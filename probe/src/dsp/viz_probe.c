/*
 * viz_probe — the device probe for Viz (DESIGN.md, build order step 0).
 *
 * Sits in a Master FX slot and never touches the audio. It drives a
 * 203 Systems Matrix (Performance app) on the Move's USB-A port with one
 * test pattern at a time, picked by the TEST knob, and measures:
 *   - how many external MIDI packets the host accepts per block;
 *   - whether the Master FX buffer carries sound (input level);
 *   - what the transport looks like from a Master FX (beat position, bpm);
 *   - whether any MIDI reaches the Master FX (e.g. Matrix pad presses);
 *   - what a Viz-sized render costs on the render thread (bench).
 * A worker thread (SCHED_OTHER) writes /data/UserData/schwung/viz-probe/report.txt
 * every two seconds. Nothing on the audio path allocates, locks or touches a file.
 */
#define _GNU_SOURCE
#include <math.h>
#include <pthread.h>
#include <sched.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "host/audio_fx_api_v2.h"
#include "host/plugin_api_v1.h"
#include "host/move_info.h"

#define SR       MOVE_SAMPLE_RATE
#define OUT_DIR  "/data/UserData/schwung/viz-probe"
#define NLIGHT   96           /* 64 pads + 32 underglow */
#define FRAME_BLOCKS 11       /* ~31 fps pattern updates */

enum {
    T_OFF, T_WALK, T_EDGE, T_PAL1, T_PAL2, T_WHITE, T_FLOOD,
    T_SYSEX_RGB, T_SYSEX_PAL, T_BEAT, T_COUNT
};
static const char *k_test_names[T_COUNT] = {
    "Off", "Walk", "Edge", "Palette 1", "Palette 2", "White", "Flood",
    "SysEx RGB", "SysEx Palette", "Beat"
};

/* ---- Matrix layout (Matrix OS Performance8x8.h:88, NoteToXY) ---------- */

/* Light id: 0..63 pads (x + 8*y, y = 0 is the top row), then underglow
 * 64..71 right (top to bottom), 72..79 left, 80..87 bottom (left to right),
 * 88..95 top. */
static uint8_t light_note(int id) {
    if (id < 64) {
        int x = id % 8, y = id / 8;
        return (uint8_t)(x < 4 ? 64 - 4 * y + x : 96 - 4 * y + (x - 4));
    }
    id -= 64;
    if (id < 8)  return (uint8_t)(100 + id);
    if (id < 16) return (uint8_t)(108 + id - 8);
    if (id < 24) return (uint8_t)(116 + id - 16);
    return (uint8_t)(28 + id - 24);
}

/* Matrix palette (channel 1) indices used by the tests. */
#define C_OFF    0
#define C_RED    5
#define C_YELLOW 25
#define C_GREEN  45
#define C_CYAN   65
#define C_BLUE   85
#define C_PINK   105
#define C_WHITE  127
static const uint8_t k_greys[8] = { 0, 121, 122, 123, 124, 125, 126, 127 };
static const uint8_t k_hues[8]  = { 5, 15, 25, 35, 45, 65, 85, 105 };

typedef struct { uint8_t ch, idx; } light_t;

typedef struct {
    /* settings */
    int      test;
    int      level;      /* 0..7: grey step for White */
    int      budget;     /* packets per block */
    int      nudge_ms;
    /* lights */
    light_t  want[NLIGHT];
    light_t  shown[NLIGHT];
    uint8_t  known[NLIGHT];  /* 0 = shown state unknown, must send */
    int      cursor;
    /* sysex queue: whole USB-MIDI packets, sent within the budget */
    uint8_t  sx[1024][4];
    int      sx_len, sx_pos;
    /* pattern clock */
    long     blocks;
    long     test_blocks;
    double   last_beat;
    int      beat_flash;  /* blocks left on the beat flash */
    int      beat_down;
    /* measurements, written by the audio thread */
    volatile long  m_attempt, m_accept, m_refuse, m_refuse_blocks, m_blocks;
    volatile int   m_max_block_accept;
    volatile double m_sumsq_l, m_sumsq_r; volatile long m_frames;
    volatile int   m_peak;
    volatile double m_beat; volatile float m_bpm; volatile int m_ts_up, m_ts_lo;
    volatile long  m_beats_seen;
    volatile long  m_midi[4]; volatile uint8_t m_midi_last[4][3];
    /* bench, written by the worker */
    double   bench_ms_noise, bench_ms_fluid;
    /* worker */
    pthread_t thread;
    int      quit;
    char     state[48];
} inst_t;

static const host_api_v1_t *g_host;
static audio_fx_api_v2_t g_api;

static void logf_(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void logf_(const char *fmt, ...) {
    if (!g_host || !g_host->log) return;
    char b[256];
    va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
    g_host->log(b);
}

/* ---- bench: a Viz-sized frame, timed on the worker --------------------- */

static float hash2(int x, int y) {
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return (float)(h & 0xffff) / 65535.0f;
}
static float vnoise(float x, float y) {
    int ix = (int)floorf(x), iy = (int)floorf(y);
    float fx = x - ix, fy = y - iy;
    fx = fx * fx * (3 - 2 * fx); fy = fy * fy * (3 - 2 * fy);
    float a = hash2(ix, iy), b = hash2(ix + 1, iy), c = hash2(ix, iy + 1), d = hash2(ix + 1, iy + 1);
    return a + (b - a) * fx + (c - a) * fy + (a - b - c + d) * fx * fy;
}
static float fbm(float x, float y) {
    float s = 0, a = 0.5f;
    for (int o = 0; o < 5; o++) { s += a * vnoise(x, y); x *= 2.03f; y *= 2.03f; a *= 0.5f; }
    return s;
}

#define FN 16
static float fu[FN * FN], fv[FN * FN], fd[FN * FN], ft[FN * FN];
static void fluid_step(void) {
    /* diffuse + advect + project: a small stable-fluids step (Stam 1999). */
    for (int it = 0; it < 20; it++)
        for (int y = 1; y < FN - 1; y++)
            for (int x = 1; x < FN - 1; x++) {
                int i = y * FN + x;
                ft[i] = (fd[i] + 0.1f * (fd[i - 1] + fd[i + 1] + fd[i - FN] + fd[i + FN])) / 1.4f;
            }
    for (int y = 1; y < FN - 1; y++)
        for (int x = 1; x < FN - 1; x++) {
            int i = y * FN + x;
            float sx = x - fu[i], sy = y - fv[i];
            sx = fminf(fmaxf(sx, 0.5f), FN - 1.5f);
            sy = fminf(fmaxf(sy, 0.5f), FN - 1.5f);
            int x0 = (int)sx, y0 = (int)sy; float a = sx - x0, b = sy - y0;
            fd[i] = (1 - b) * ((1 - a) * ft[y0 * FN + x0] + a * ft[y0 * FN + x0 + 1])
                  + b * ((1 - a) * ft[(y0 + 1) * FN + x0] + a * ft[(y0 + 1) * FN + x0 + 1]);
        }
    for (int it = 0; it < 20; it++)
        for (int y = 1; y < FN - 1; y++)
            for (int x = 1; x < FN - 1; x++) {
                int i = y * FN + x;
                float div = (fu[i + 1] - fu[i - 1] + fv[i + FN] - fv[i - FN]) * 0.5f;
                ft[i] = (ft[i - 1] + ft[i + 1] + ft[i - FN] + ft[i + FN] - div) * 0.25f;
            }
    for (int y = 1; y < FN - 1; y++)
        for (int x = 1; x < FN - 1; x++) {
            int i = y * FN + x;
            fu[i] -= 0.5f * (ft[i + 1] - ft[i - 1]);
            fv[i] -= 0.5f * (ft[i + FN] - ft[i - FN]);
        }
}

static double now_ms(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000.0 + t.tv_nsec / 1e6;
}

static void run_bench(inst_t *s) {
    volatile float sink = 0;
    double t0 = now_ms();
    for (int f = 0; f < 120; f++)          /* 32x32 field, domain-warped fbm */
        for (int y = 0; y < 32; y++)
            for (int x = 0; x < 32; x++) {
                float u = x / 8.0f, v = y / 8.0f, t = f * 0.03f;
                float w = fbm(u + t, v - t);
                sink += fbm(u + 2.0f * w, v + 2.0f * w + t);
            }
    s->bench_ms_noise = (now_ms() - t0) / 120.0;
    for (int i = 0; i < FN * FN; i++) { fu[i] = hash2(i, 1) - 0.5f; fv[i] = hash2(i, 2) - 0.5f; fd[i] = hash2(i, 3); }
    t0 = now_ms();
    for (int f = 0; f < 120; f++) fluid_step();
    s->bench_ms_fluid = (now_ms() - t0) / 120.0;
    (void)sink;
}

/* ---- worker: bench once, then the report every 2 s -------------------- */

static void write_report(inst_t *s) {
    mkdir(OUT_DIR, 0755);
    FILE *f = fopen(OUT_DIR "/report.txt", "w");
    if (!f) return;
    double n = s->m_frames > 0 ? (double)s->m_frames : 1;
    double rl = sqrt(s->m_sumsq_l / n) / 32768.0, rr = sqrt(s->m_sumsq_r / n) / 32768.0;
    fprintf(f,
        "test %s (budget %d, level %d, nudge %d ms)\n"
        "packets: attempted %ld accepted %ld refused %ld\n"
        "blocks %ld, blocks with a refusal %ld, most accepted in one block %d\n"
        "input over the last 2 s: rms L %.1f dBFS R %.1f dBFS, peak %d\n"
        "beat position %.3f, bpm %.2f, time signature %d/%d, beats seen %ld\n"
        "midi to this Master FX: internal %ld [%02x %02x %02x], external %ld [%02x %02x %02x], host %ld\n"
        "bench: 32x32 warped fbm %.3f ms/frame, 16x16 fluid step %.3f ms/frame\n",
        k_test_names[s->test], s->budget, s->level, s->nudge_ms,
        s->m_attempt, s->m_accept, s->m_refuse,
        s->m_blocks, s->m_refuse_blocks, s->m_max_block_accept,
        rl > 0 ? 20 * log10(rl) : -999.0, rr > 0 ? 20 * log10(rr) : -999.0, s->m_peak,
        s->m_beat, (double)s->m_bpm, s->m_ts_up, s->m_ts_lo, s->m_beats_seen,
        s->m_midi[0], s->m_midi_last[0][0], s->m_midi_last[0][1], s->m_midi_last[0][2],
        s->m_midi[2], s->m_midi_last[2][0], s->m_midi_last[2][1], s->m_midi_last[2][2],
        s->m_midi[3],
        s->bench_ms_noise, s->bench_ms_fluid);
    fclose(f);
    logf_("viz-probe: %s acc %ld ref %ld max/blk %d rms %.1f beat %.2f midi i%ld e%ld",
          k_test_names[s->test], s->m_accept, s->m_refuse, s->m_max_block_accept,
          rl > 0 ? 20 * log10(rl) : -999.0, s->m_beat, s->m_midi[0], s->m_midi[2]);
    /* the level window restarts; packet counters are per test */
    s->m_sumsq_l = s->m_sumsq_r = 0; s->m_frames = 0; s->m_peak = 0;
}

static void *worker(void *arg) {
    inst_t *s = arg;
    run_bench(s);
    logf_("viz-probe: bench noise %.3f ms fluid %.3f ms", s->bench_ms_noise, s->bench_ms_fluid);
    int tick = 0;
    while (!__atomic_load_n(&s->quit, __ATOMIC_ACQUIRE)) {
        if (++tick % 40 == 0) write_report(s);
        usleep(50 * 1000);
    }
    return NULL;
}

/* ---- sending ----------------------------------------------------------- */

static int send_pkt(const uint8_t p[4]) {
    return g_host->midi_send_external ? g_host->midi_send_external(p, 4) : 0;
}

static void sysex_queue(inst_t *s, const uint8_t *b, int n) {
    s->sx_len = s->sx_pos = 0;
    for (int i = 0; i < n && s->sx_len < 1024; i += 3) {
        int left = n - i;
        uint8_t *p = s->sx[s->sx_len++];
        memset(p, 0, 4);
        if (left > 3) { p[0] = 0x24; memcpy(p + 1, b + i, 3); }
        else { p[0] = (uint8_t)(0x24 + left); memcpy(p + 1, b + i, (size_t)left); }
    }
}

/* Sends queued SysEx first, then changed lights, within the budget.
 * A refused packet is never recorded as shown. */
static void pump(inst_t *s) {
    int sent = 0, refused = 0;
    while (sent < s->budget && s->sx_pos < s->sx_len) {
        s->m_attempt++;
        if (!send_pkt(s->sx[s->sx_pos])) { refused = 1; s->m_refuse++; break; }
        s->sx_pos++; sent++; s->m_accept++;
    }
    for (int k = 0; k < NLIGHT && sent < s->budget && !refused; k++) {
        int id = (s->cursor + k) % NLIGHT;
        light_t w = s->want[id];
        if (s->known[id] && s->shown[id].ch == w.ch && s->shown[id].idx == w.idx) continue;
        /* a note on any channel repaints the pad, so one packet is enough */
        uint8_t p[4] = { 0x29, (uint8_t)(0x90 | w.ch), light_note(id), w.idx };
        s->m_attempt++;
        if (!send_pkt(p)) { refused = 1; s->m_refuse++; s->cursor = id; break; }
        s->shown[id] = w; s->known[id] = 1; sent++; s->m_accept++;
        s->cursor = (id + 1) % NLIGHT;
    }
    s->m_blocks++;
    if (refused) s->m_refuse_blocks++;
    if (sent > s->m_max_block_accept) s->m_max_block_accept = sent;
}

/* ---- patterns ----------------------------------------------------------- */

static void fill(inst_t *s, int from, int to, uint8_t ch, uint8_t idx) {
    for (int i = from; i < to; i++) { s->want[i].ch = ch; s->want[i].idx = idx; }
}

static void corners(inst_t *s) {
    s->want[0].idx = C_RED; s->want[7].idx = C_GREEN;
    s->want[56].idx = C_BLUE; s->want[63].idx = C_WHITE;
}

static void sysex_rgb(inst_t *s, int phase) {
    /* F0 00 02 03 4D 58 5E (index r g b)... F7: a rainbow, 6-bit RGB per pad */
    uint8_t b[300]; int n = 0;
    const uint8_t hdr[] = { 0xF0, 0x00, 0x02, 0x03, 0x4D, 0x58, 0x5E };
    memcpy(b, hdr, sizeof hdr); n = sizeof hdr;
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++) {
            float h = (x + y + phase) / 16.0f * 6.2831853f;
            b[n++] = (uint8_t)((8 - y) * 10 + (x + 1));
            b[n++] = (uint8_t)(31.5f + 31.5f * cosf(h));
            b[n++] = (uint8_t)(31.5f + 31.5f * cosf(h - 2.094f));
            b[n++] = (uint8_t)(31.5f + 31.5f * cosf(h + 2.094f));
        }
    b[n++] = 0xF7;
    sysex_queue(s, b, n);
}

static void sysex_palette(inst_t *s) {
    /* Write custom palette 1 (channel 3) in RAM: 41 3D 00 (idx r g b)...
     * Never 41 7D, which would save it to flash. Black -> ember -> white. */
    static uint8_t b[600]; int n = 0;
    const uint8_t hdr[] = { 0xF0, 0x00, 0x02, 0x03, 0x4D, 0x58, 0x41, 0x3D, 0x00 };
    memcpy(b, hdr, sizeof hdr); n = sizeof hdr;
    for (int i = 0; i < 128; i++) {
        float t = i / 127.0f;
        float r = fminf(1, t * 2.0f), g = fminf(1, fmaxf(0, t * 2.0f - 0.6f)), bl = fmaxf(0, t * 2.0f - 1.2f);
        b[n++] = (uint8_t)i;
        b[n++] = (uint8_t)(r * 63); b[n++] = (uint8_t)(g * 63); b[n++] = (uint8_t)(fminf(1, bl) * 63);
    }
    b[n++] = 0xF7;
    sysex_queue(s, b, n);
}

static void pattern(inst_t *s, double beat) {
    long t = s->test_blocks;
    int frame = (t % FRAME_BLOCKS) == 0;
    switch (s->test) {
    case T_OFF:
        fill(s, 0, NLIGHT, 0, C_OFF);
        break;
    case T_WALK: /* corners steady; a yellow pad walks in reading order, 4 per second */
        if (!frame) break;
        fill(s, 0, NLIGHT, 0, C_OFF);
        corners(s);
        s->want[(t / 86) % 64].idx = C_YELLOW;
        break;
    case T_EDGE: /* top red, right green, bottom blue, left yellow; a white light walks the ring */
        if (!frame) break;
        fill(s, 0, 64, 0, C_OFF);
        corners(s);
        fill(s, 88, 96, 0, C_RED);
        fill(s, 64, 72, 0, C_GREEN);
        fill(s, 80, 88, 0, C_BLUE);
        fill(s, 72, 80, 0, C_YELLOW);
        {
            /* clockwise from top-left: top L->R, right T->B, bottom R->L, left B->T */
            int k = (int)((t / 86) % 32), id;
            if (k < 8) id = 88 + k;
            else if (k < 16) id = 64 + (k - 8);
            else if (k < 24) id = 80 + (7 - (k - 16));
            else id = 72 + (7 - (k - 24));
            s->want[id].idx = C_WHITE;
        }
        break;
    case T_PAL1:
    case T_PAL2: /* indices 0..63 for 3 s, then 64..127, in reading order */
        if (!frame) break;
        {
            int half = (int)((t / 1034) % 2);
            uint8_t ch = s->test == T_PAL1 ? 0 : 1;
            for (int i = 0; i < 64; i++) { s->want[i].ch = ch; s->want[i].idx = (uint8_t)(half * 64 + i); }
            fill(s, 64, NLIGHT, ch, (uint8_t)(half ? C_WHITE : C_OFF));
        }
        break;
    case T_WHITE: /* power: every light at one grey step (LEVEL) */
        fill(s, 0, NLIGHT, 0, k_greys[s->level & 7]);
        break;
    case T_FLOOD: /* every light changes colour every block: the throughput test */
        {
            uint8_t c = k_hues[t % 8];
            fill(s, 0, NLIGHT, 0, c);
        }
        break;
    case T_SYSEX_RGB: /* rainbow by SysEx every 2 s; pads stay dark if SysEx never lands */
        if (t == 0) fill(s, 0, NLIGHT, 0, C_OFF);
        if (t % 689 == 10) sysex_rgb(s, (int)(t / 689));
        break;
    case T_SYSEX_PAL: /* upload a gradient palette, then paint it on channel 3.
                         All white = SysEx did not land (the app's default). */
        if (t == 0) { fill(s, 0, NLIGHT, 0, C_OFF); sysex_palette(s); }
        if (t == 200)
            for (int i = 0; i < 64; i++) { s->want[i].ch = 2; s->want[i].idx = (uint8_t)(i * 2 + 1); }
        break;
    case T_BEAT: /* white flash on each beat, red on the downbeat; NUDGE shifts it */
        {
            if (beat >= 0) {
                double bpm = s->m_bpm > 0 ? s->m_bpm : 120.0;
                double b = beat + s->nudge_ms / 1000.0 * bpm / 60.0;
                if (s->last_beat >= 0 && floor(b) != floor(s->last_beat)) {
                    int up = s->m_ts_up > 0 ? s->m_ts_up : 4;
                    s->beat_down = ((long)floor(b) % up) == 0;
                    s->beat_flash = 30;  /* ~87 ms */
                    s->m_beats_seen++;
                }
                s->last_beat = b;
            } else {
                s->last_beat = -1;
            }
            uint8_t c = C_OFF;
            if (s->beat_flash > 0) {
                c = s->beat_down ? C_RED : C_WHITE;
                if (s->beat_flash < 20) c = s->beat_down ? 3 : 124;
                if (s->beat_flash < 10) c = s->beat_down ? 1 : 121;
                s->beat_flash--;
            }
            fill(s, 0, NLIGHT, 0, c);
        }
        break;
    }
}

/* ---- v2 API --------------------------------------------------------------- */

static void reset_test(inst_t *s, int test) {
    s->test = test;
    s->test_blocks = 0;
    s->sx_len = s->sx_pos = 0;
    s->budget = test == T_FLOOD ? 20 : 8;
    s->m_attempt = s->m_accept = s->m_refuse = s->m_refuse_blocks = s->m_blocks = 0;
    s->m_max_block_accept = 0;
    s->m_beats_seen = 0;
    s->last_beat = -1;
    memset(s->known, 0, sizeof s->known);  /* repaint everything */
    snprintf(s->state, sizeof s->state, "%s", k_test_names[test]);
}

static void *v2_create_instance(const char *module_dir, const char *config_json) {
    (void)module_dir; (void)config_json;
    move_info_t mi;
    move_info_read(&mi);  /* first call resolves the export; off the callback */
    inst_t *s = calloc(1, sizeof *s);
    if (!s) return NULL;
    s->level = 4;
    reset_test(s, T_WALK);

    pthread_attr_t a;
    pthread_attr_init(&a);
    pthread_attr_setinheritsched(&a, PTHREAD_EXPLICIT_SCHED);
    pthread_attr_setschedpolicy(&a, SCHED_OTHER);
    struct sched_param sp = { 0 };
    pthread_attr_setschedparam(&a, &sp);
    if (pthread_create(&s->thread, &a, worker, s) != 0) { free(s); s = NULL; }
    pthread_attr_destroy(&a);
    logf_("viz-probe: create %s", s ? "ok" : "failed");
    return s;
}

static void v2_destroy_instance(void *inst) {
    inst_t *s = inst;
    if (!s) return;
    __atomic_store_n(&s->quit, 1, __ATOMIC_RELEASE);
    pthread_join(s->thread, NULL);
    free(s);
}

static void v2_process_block(void *inst, int16_t *io, int frames) {
    inst_t *s = inst;
    /* listen: level only, the buffer is left exactly as it came */
    double sl = 0, sr = 0; int pk = s->m_peak;
    for (int i = 0; i < frames; i++) {
        int l = io[2 * i], r = io[2 * i + 1];
        sl += (double)l * l; sr += (double)r * r;
        if (abs(l) > pk) pk = abs(l);
        if (abs(r) > pk) pk = abs(r);
    }
    s->m_sumsq_l += sl; s->m_sumsq_r += sr; s->m_frames += frames; s->m_peak = pk;

    move_info_t mi;
    if (move_info_read(&mi) && mi.valid) { s->m_ts_up = mi.ts_upper; s->m_ts_lo = mi.ts_lower; }
    double beat = g_host->get_beat_position ? g_host->get_beat_position() : -1.0;
    s->m_beat = beat;
    s->m_bpm = g_host->get_bpm ? g_host->get_bpm() : 0.0f;

    pattern(s, beat);
    pump(s);
    s->test_blocks++;
    s->blocks++;
}

static void v2_on_midi(void *inst, const uint8_t *msg, int len, int source) {
    inst_t *s = inst;
    int k = source >= 0 && source < 4 ? source : 3;
    s->m_midi[k]++;
    for (int i = 0; i < 3; i++) s->m_midi_last[k][i] = i < len ? msg[i] : 0;
}

static int parse_enum(const char *val, const char *const *names, int n) {
    for (int i = 0; i < n; i++) if (!strcmp(val, names[i])) return i;
    int v = atoi(val);
    return v < 0 ? 0 : v >= n ? n - 1 : v;
}

static void v2_set_param(void *inst, const char *key, const char *val) {
    inst_t *s = inst;
    if (!strcmp(key, "test")) {
        int t = parse_enum(val, k_test_names, T_COUNT);
        if (t != s->test) reset_test(s, t);
    } else if (!strcmp(key, "level")) {
        int v = atoi(val); s->level = v < 0 ? 0 : v > 7 ? 7 : v;
    } else if (!strcmp(key, "nudge")) {
        int v = atoi(val); s->nudge_ms = v < -200 ? -200 : v > 200 ? 200 : v;
    }
}

static int v2_get_param(void *inst, const char *key, char *buf, int len) {
    inst_t *s = inst;
    if (!strcmp(key, "test")) return snprintf(buf, len, "%s", k_test_names[s->test]);
    if (!strcmp(key, "level")) return snprintf(buf, len, "%d", s->level);
    if (!strcmp(key, "nudge")) return snprintf(buf, len, "%d", s->nudge_ms);
    if (!strcmp(key, "chain_params"))
        return snprintf(buf, len,
            "[{\"key\":\"test\",\"name\":\"Test pattern\",\"short_name\":\"TEST\",\"type\":\"enum\","
            "\"options\":[\"Off\",\"Walk\",\"Edge\",\"Palette 1\",\"Palette 2\",\"White\",\"Flood\","
            "\"SysEx RGB\",\"SysEx Palette\",\"Beat\"],\"default\":\"Walk\"},"
            "{\"key\":\"level\",\"name\":\"White level\",\"short_name\":\"LEVEL\",\"type\":\"int\","
            "\"min\":0,\"max\":7,\"default\":4},"
            "{\"key\":\"nudge\",\"name\":\"Beat nudge (ms)\",\"short_name\":\"NUDGE\",\"type\":\"int\","
            "\"min\":-200,\"max\":200,\"default\":0,\"step\":5}]");
    if (!strcmp(key, "ui_hierarchy"))
        return snprintf(buf, len,
            "{\"modes\":null,\"levels\":{\"root\":{\"label\":\"Viz Probe\","
            "\"knobs\":[\"test\",\"level\",\"nudge\"],"
            "\"params\":[{\"key\":\"test\",\"label\":\"Test\"},{\"key\":\"level\",\"label\":\"Level\"},"
            "{\"key\":\"nudge\",\"label\":\"Nudge\"}]}}}");
    if (!strcmp(key, "state"))
        return snprintf(buf, len, "{\"test\":\"%s\",\"level\":%d,\"nudge\":%d}",
                        k_test_names[s->test], s->level, s->nudge_ms);
    if (len > 0) buf[0] = '\0';
    return 0;
}

audio_fx_api_v2_t *move_audio_fx_init_v2(const host_api_v1_t *host) {
    g_host = host;
    memset(&g_api, 0, sizeof g_api);
    g_api.api_version      = AUDIO_FX_API_VERSION_2;
    g_api.create_instance  = v2_create_instance;
    g_api.destroy_instance = v2_destroy_instance;
    g_api.process_block    = v2_process_block;
    g_api.set_param        = v2_set_param;
    g_api.get_param        = v2_get_param;
    g_api.on_midi          = v2_on_midi;
    return &g_api;
}
