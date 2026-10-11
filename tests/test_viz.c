/*
 * Viz bench tests (DESIGN.md, "Testing").
 *
 * The MIDI and control tests drive the module black-box through the v2 API,
 * with a fake host that records every packet, can refuse packets, and serves
 * a scripted clock. The instance is created with {"sync_render":1}, so frames
 * are rendered in step with the audio and every run is repeatable. The
 * listening and scene tests call the listening and rendering code directly.
 *
 * Usage: test_viz <dump dir>   (writes chain_params.json and ui_hierarchy.json there)
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "host/audio_fx_api_v2.h"
#include "host/plugin_api_v1.h"
#include "device.h"
#include "seq.h"
#include "viz.h"

void move_audio_fx_on_midi(void *inst, const uint8_t *msg, int len, int source);

extern audio_fx_api_v2_t *move_audio_fx_init_v2(const host_api_v1_t *host);

static int g_fail = 0;
#define CHECK(cond, ...) do { if (!(cond)) { g_fail++; fprintf(stderr, "FAIL: " __VA_ARGS__); fputc('\n', stderr); } } while (0)

/* ---- the fake host ---------------------------------------------------- */

static double g_beat = -1;          /* < 0: stopped */
static float g_bpm = 120;
static int g_refuse_every = 0;      /* refuse every Nth packet */
static long g_calls, g_block_pkts, g_max_block_pkts, g_total_pkts;
static int g_bad_cable;

/* What a Matrix (or Launchpad) would show, rebuilt from the accepted packets. */
static uint8_t g_sx[1024];
static int g_sx_len;
static uint8_t g_disp[256][3];      /* by grid index */
static long g_last_sent[256];       /* block of the last message that set it */
static long g_block;
static int g_bad_msg;
static int g_grid = GRID_MATRIX;
static int g_setups;

static int index_valid(int grid, int idx) {
    for (int id = 0; id < NLIGHT; id++) if (viz_grid_index(grid, id) == idx) return 1;
    return 0;
}

static void on_message(const uint8_t *m, int n) {
    const viz_grid_t *g = &viz_grids[g_grid];
    if (getenv("VIZ_DUMP")) { fprintf(stderr, "msg %d:", n); for (int i = 0; i < n && i < 24; i++) fprintf(stderr, " %02X", m[i]); fprintf(stderr, "\n"); }
    if (n < 8 || m[0] != 0xF0 || m[n - 1] != 0xF7) { g_bad_msg++; if (getenv("VIZ_DUMP")) fprintf(stderr, "bad@%d\n", __LINE__); return; }
    if (memcmp(m + 1, g->hdr, 5)) {
        /* the last message to the previous grid, finished after a GRID change */
        for (int k = 0; k < GRID_COUNT; k++) if (!memcmp(m + 1, viz_grids[k].hdr, 5)) return;
        g_bad_msg++;
        return;
    }
    const uint8_t *b = m + 6;
    int len = n - 7;
    if (g->setup_len && len == g->setup_len && !memcmp(b, g->setup, (size_t)len)) { g_setups++; return; }
    if (b[0] != g->cmd) { g_bad_msg++; if (getenv("VIZ_DUMP")) fprintf(stderr, "bad@%d\n", __LINE__); return; }
    b++; len--;
    int per = g->spec ? 5 : 4, maxv = (1 << g->bits) - 1;
    if (len % per) { g_bad_msg++; if (getenv("VIZ_DUMP")) fprintf(stderr, "bad@%d\n", __LINE__); return; }
    for (int i = 0; i < len; i += per) {
        const uint8_t *e = b + i;
        if (g->spec) { if (e[0] != 0x03) { g_bad_msg++; if (getenv("VIZ_DUMP")) fprintf(stderr, "bad@%d\n", __LINE__); return; } e++; }
        if (e[1] > maxv || e[2] > maxv || e[3] > maxv) { g_bad_msg++; if (getenv("VIZ_DUMP")) fprintf(stderr, "bad@%d\n", __LINE__); return; }
        if (e[0] == 0 && g->fill) {
            for (int id = 0; id < NLIGHT; id++) {
                int ix = viz_grid_index(g_grid, id);
                if (ix) { memcpy(g_disp[ix], e + 1, 3); g_last_sent[ix] = g_block; }
            }
        } else if (!index_valid(g_grid, e[0])) {
            if (getenv("VIZ_DUMP")) fprintf(stderr, "bad index %d on %s\n", e[0], g->name);
            g_bad_msg++;
            return;
        } else {
            memcpy(g_disp[e[0]], e + 1, 3);
            g_last_sent[e[0]] = g_block;
        }
    }
}

/* A channel message on the Matrix: Performance paints it from a palette, even
 * between the packets of a SysEx message. */
static long g_note_pkts;
static void paint_note(const uint8_t m[3]) {
    int id = viz_grid_painted(GRID_MATRIX, m);
    if (id < 0 || g_grid != GRID_MATRIX) return;
    int ch = m[0] & 15, vel = (m[0] & 0xF0) == 0x80 ? 0 : m[2];
    const uint8_t *c = ch < 2 ? viz_matrix_palette[ch][vel] : (const uint8_t[3]){ 0, 0, 0 };
    uint8_t *d = g_disp[viz_grid_index(GRID_MATRIX, id)];
    for (int k = 0; k < 3; k++) d[k] = c[k] >> 2;
}

static int fake_send(const uint8_t *p, int len) {
    (void)len;
    g_calls++;
    if (g_refuse_every && g_calls % g_refuse_every == 0) return 0;
    if ((p[0] >> 4) != 2) g_bad_cable++;
    int cin = p[0] & 15, n = cin == 4 ? 3 : cin >= 5 && cin <= 7 ? cin - 4 : 0;
    if (cin == 8 || cin == 9 || cin == 0xB) { g_note_pkts++; paint_note(p + 1); return 4; }
    if (!n) { g_bad_cable++; return 4; }
    for (int i = 0; i < n && g_sx_len < (int)sizeof g_sx; i++) {
        if (p[1 + i] == 0xF0) g_sx_len = 0;            /* a device restarts on F0 */
        g_sx[g_sx_len++] = p[1 + i];
    }
    if (cin != 4) {
        if (g_sx_len && g_sx[0] == 0xF0) on_message(g_sx, g_sx_len);   /* a tail from before a reset is noise */
        g_sx_len = 0;
    }
    g_block_pkts++;
    g_total_pkts++;
    return 4;
}

static double fake_beat(void) { return g_beat; }
static float fake_bpm(void) { return g_bpm; }
static void fake_log(const char *m) { (void)m; }

static host_api_v1_t g_host;
static audio_fx_api_v2_t *api;

static void reset_display(int grid) {
    memset(g_disp, 0, sizeof g_disp);
    memset(g_last_sent, 0, sizeof g_last_sent);
    g_sx_len = 0;
    g_grid = grid;
    g_bad_msg = g_bad_cable = 0;
    g_max_block_pkts = g_total_pkts = 0;
    g_setups = 0;
}

/* ---- test audio ------------------------------------------------------- */

typedef struct { double t; uint32_t rng; } synth_t;

/* A 120 BPM kick, hats on the 8ths, and a soft pad: 'music'. */
static void music_block(synth_t *s, int16_t *io, int kick, int hats, int pad) {
    for (int i = 0; i < VIZ_BLOCK; i++) {
        double t = s->t + (double)i / VIZ_SR;
        double bp = fmod(t, 0.5), ep = fmod(t, 0.25);
        float v = 0;
        if (kick) v += (float)(sin(2 * M_PI * (50 + 60 * exp(-bp * 30)) * bp) * exp(-bp * 10) * 0.6);
        if (hats) { float n = viz_randf(&s->rng) * 2 - 1; v += (float)(n * exp(-ep * 120) * 0.15); }
        if (pad) v += (float)(sin(2 * M_PI * 330 * t) * 0.05 + sin(2 * M_PI * 440 * t) * 0.04);
        int16_t x = (int16_t)(v * 26000);
        io[2 * i] = io[2 * i + 1] = x;
    }
    s->t += (double)VIZ_BLOCK / VIZ_SR;
}

static void sine_block(double *ph, double hz, int16_t *io) {
    for (int i = 0; i < VIZ_BLOCK; i++) {
        *ph += 2 * M_PI * hz / VIZ_SR;
        int16_t x = (int16_t)(sin(*ph) * 12000);
        io[2 * i] = io[2 * i + 1] = x;
    }
}

/* Run the instance for some seconds of music; the clock runs if playing. */
static void run(void *inst, synth_t *s, double seconds, int playing, int music) {
    int16_t io[VIZ_BLOCK * 2];
    long blocks = (long)(seconds * VIZ_SR / VIZ_BLOCK);
    for (long b = 0; b < blocks; b++) {
        if (music) music_block(s, io, 1, 1, 1); else memset(io, 0, sizeof io);
        g_beat = playing ? s->t * g_bpm / 60.0 : -1;
        g_block_pkts = 0;
        g_block++;
        api->process_block(inst, io, VIZ_BLOCK);
        if (g_block_pkts > g_max_block_pkts) g_max_block_pkts = g_block_pkts;
    }
}

static void *make(void) { return api->create_instance(".", "{\"sync_render\":1}"); }

/* ---- MIDI tests ------------------------------------------------------- */

static double display_power(int grid) {
    double sum = 0;
    int shift = 8 - viz_grids[grid].bits;
    for (int id = 0; id < NLIGHT; id++) {
        int ix = viz_grid_index(grid, id);
        if (!ix) continue;
        double w = id < NPAD ? 1.0 : viz_grids[grid].edge_weight;
        for (int k = 0; k < 3; k++) sum += w * (g_disp[ix][k] << shift);
    }
    return sum;
}

static void test_midi(void) {
    synth_t s = { 0, 1 };
    void *v = make();
    reset_display(GRID_MATRIX);
    long t0 = g_total_pkts;
    api->set_param(v, "light", "1");
    api->set_param(v, "color", "Mono");            /* white over the whole grid: the guard has work */
    api->set_param(v, "size", "4");
    double worst = 0;
    for (int k = 0; k < 54; k++) {                 /* 20 s, looking off the beat */
        run(v, &s, 0.37, 1, 1);
        double p = display_power(GRID_MATRIX);
        if (p > worst) worst = p;
    }
    double secs = 54 * 0.37;
    CHECK(g_max_block_pkts <= SEND_PER_BLOCK_MAX, "a block sent %ld packets (max %d)", g_max_block_pkts, SEND_PER_BLOCK_MAX);
    CHECK((g_total_pkts - t0) / secs <= SEND_PER_SECOND * 1.01, "%.0f packets/s", (g_total_pkts - t0) / secs);
    CHECK(g_bad_cable == 0, "%d packets not on cable 2 SysEx", g_bad_cable);
    CHECK(g_bad_msg == 0, "%d SysEx messages did not parse", g_bad_msg);
    /* 6-bit values come from flooring 8-bit ones, so the display can read up
     * to 3 above per channel once expanded: allow that much. */
    CHECK(worst <= POWER_BUDGET + 3 * 3 * 128, "power %.0f over the budget %.0f", worst, (double)POWER_BUDGET);
    printf("  matrix: %.0f packets/s, max %ld per block, worst power %.0f of %.0f\n",
           (g_total_pkts - t0) / secs, g_max_block_pkts, worst, (double)POWER_BUDGET);

    /* POWER Hub: the guard is off, so a bright scene goes over. */
    api->set_param(v, "power", "Hub");
    api->set_param(v, "scene", "Pulse");
    double hub = 0;
    for (int k = 0; k < 40; k++) { run(v, &s, 0.13, 1, 1); double p = display_power(GRID_MATRIX); if (p > hub) hub = p; }
    CHECK(hub > POWER_BUDGET, "POWER Hub never went over the budget (%.0f)", hub);
    api->set_param(v, "power", "Port");

    /* Every light is re-sent within 2 s, even when nothing changes. */
    api->set_param(v, "light", "0");
    run(v, &s, 2.0, 1, 1);
    long from = g_block;
    run(v, &s, 2.2, 1, 1);
    int stale = 0;
    for (int id = 0; id < NLIGHT; id++) if (g_last_sent[viz_grid_index(GRID_MATRIX, id)] <= from) stale++;
    CHECK(stale == 0, "%d lights not re-sent within 2.2 s", stale);
    int lit = 0;
    for (int id = 0; id < NLIGHT; id++) { uint8_t *c = g_disp[viz_grid_index(GRID_MATRIX, id)]; lit += c[0] || c[1] || c[2]; }
    CHECK(lit == 0, "LIGHT 0 left %d lights lit", lit);
    api->destroy_instance(v);
}

/* Move's own notes go out of USB-A to the Matrix as well. Viz covers each
 * one in the same transfer, then sends the light again exactly. */
static void move_note(void *v, uint8_t st, uint8_t note, uint8_t vel, int source) {
    uint8_t m[3] = { st, note, vel };
    if (source == MOVE_MIDI_SOURCE_EXTERNAL) paint_note(m);   /* Move's packet comes first */
    move_audio_fx_on_midi(v, m, 3, source);                   /* how the host calls a Master FX */
}

static uint8_t g_kept[4];
static int keep(void *ctx, const uint8_t p[4]) { (void)ctx; memcpy(g_kept, p, 4); return 1; }

static long covers(void *v) { char b[32]; api->get_param(v, "covers", b, sizeof b); return atol(b); }

static void test_move_notes(void) {
    /* the note map: Performance's drum rack, and the edge */
    static const struct { uint8_t note; int id; } map[] = {
        { 36, 56 }, { 39, 59 }, { 40, 48 }, { 67, 3 }, { 68, 60 }, { 99, 7 },
        { 100, NPAD + EDGE_RIGHT * 8 }, { 115, NPAD + EDGE_LEFT * 8 + 7 },
        { 116, NPAD + EDGE_BOTTOM * 8 }, { 35, NPAD + EDGE_TOP * 8 + 7 },
    };
    for (size_t i = 0; i < sizeof map / sizeof map[0]; i++) {
        uint8_t m[3] = { 0x90, map[i].note, 100 };
        CHECK(viz_grid_painted(GRID_MATRIX, m) == map[i].id, "note %d paints light %d, not %d",
              map[i].note, viz_grid_painted(GRID_MATRIX, m), map[i].id);
    }
    uint8_t ch7[3] = { 0x96, 36, 100 }, ch6[3] = { 0x95, 36, 100 }, cc[3] = { 0xB0, 36, 5 }, pb[3] = { 0xE0, 36, 5 };
    CHECK(viz_grid_painted(GRID_MATRIX, ch7) < 0, "channel 7 paints nothing on the Matrix");
    CHECK(viz_grid_painted(GRID_MATRIX, ch6) == 56, "channel 6 paints");
    CHECK(viz_grid_painted(GRID_MATRIX, cc) == 56, "a CC paints");
    CHECK(viz_grid_painted(GRID_MATRIX, pb) < 0, "pitch bend paints nothing");

    /* the cover colour: never brighter, exact for a palette colour */
    uint32_t rng = 7;
    int brighter = 0, inexact = 0;
    for (int k = 0; k < 2000; k++) {
        uint8_t c[3] = { (uint8_t)(viz_randf(&rng) * 255), (uint8_t)(viz_randf(&rng) * 255), (uint8_t)(viz_randf(&rng) * 255) };
        int ch, vel;
        viz_matrix_nearest(c, &ch, &vel);
        const uint8_t *q = viz_matrix_palette[ch][vel];
        brighter += q[0] + q[1] + q[2] > c[0] + c[1] + c[2];
    }
    for (int p = 0; p < 2; p++)
        for (int v = 0; v < 128; v++) {
            int ch, vel;
            viz_matrix_nearest(viz_matrix_palette[p][v], &ch, &vel);
            inexact += memcmp(viz_matrix_palette[ch][vel], viz_matrix_palette[p][v], 3) != 0;
        }
    CHECK(brighter == 0, "%d covers brighter than the light", brighter);
    CHECK(inexact == 0, "%d palette colours not covered exactly", inexact);

    synth_t s = { 0, 9 };
    void *v = make();
    reset_display(GRID_MATRIX);

    /* All dark: every note Move plays on the drum pads is dark again within
     * the same transfer. */
    api->set_param(v, "light", "0");
    run(v, &s, 1.0, 1, 1);
    int shown = 0, hits = 0;
    for (int k = 0; k < 64; k++) {
        uint8_t note = (uint8_t)(36 + k % 16);
        move_note(v, 0x90, note, (uint8_t)(5 + k % 100), MOVE_MIDI_SOURCE_EXTERNAL);
        uint8_t m[3] = { 0x90, note, 1 };
        uint8_t *d = g_disp[viz_grid_index(GRID_MATRIX, viz_grid_painted(GRID_MATRIX, m))];
        shown += d[0] || d[1] || d[2];
        hits++;
        run(v, &s, 0.06, 1, 1);
        move_note(v, 0x80, note, 64, MOVE_MIDI_SOURCE_EXTERNAL);
        run(v, &s, 0.06, 1, 1);
    }
    CHECK(shown == 0, "%d of %d Move notes left showing past their transfer", shown, hits);
    CHECK(g_bad_msg == 0, "covers broke %d SysEx messages", g_bad_msg);

    /* Lit: each hit light is sent again, exactly, within 80 ms. */
    api->set_param(v, "light", "1");
    api->set_param(v, "scene", "Pulse");
    run(v, &s, 1.0, 1, 1);
    int late = 0;
    for (int k = 0; k < 32; k++) {
        uint8_t note = (uint8_t)(36 + (k * 5) % 64);
        uint8_t m[3] = { 0x90, note, 1 };
        int ix = viz_grid_index(GRID_MATRIX, viz_grid_painted(GRID_MATRIX, m));
        long at = g_block;
        move_note(v, 0x91, note, 3, MOVE_MIDI_SOURCE_EXTERNAL);
        run(v, &s, 0.08, 1, 1);
        late += g_last_sent[ix] <= at;
    }
    CHECK(late == 0, "%d of 32 hit lights not sent again within 80 ms", late);
    CHECK(g_bad_msg == 0, "covers broke %d SysEx messages", g_bad_msg);

    /* What doesn't reach the Matrix, or is Viz's own, draws no cover. */
    long c0 = covers(v);
    move_note(v, 0x96, 40, 100, MOVE_MIDI_SOURCE_EXTERNAL);    /* channel 7 */
    move_note(v, 0x90, 70, 100, MOVE_MIDI_SOURCE_INTERNAL);    /* Move's own pad */
    move_note(v, 0xE0, 40, 100, MOVE_MIDI_SOURCE_EXTERNAL);    /* pitch bend */
    CHECK(covers(v) == c0, "%ld covers for messages that never reach the Matrix", covers(v) - c0);
    g_note_pkts = 0;
    move_note(v, 0x90, 41, 100, MOVE_MIDI_SOURCE_EXTERNAL);
    CHECK(covers(v) == c0 + 1 && g_note_pkts == 1, "one Move note, %ld covers", covers(v) - c0);
    run(v, &s, 0.01, 1, 1);
    api->destroy_instance(v);

    /* An echo: the very packet Viz sent comes back through on_midi. */
    {
        viz_sender_t snd;
        viz_sender_init(&snd, GRID_MATRIX);
        viz_frame_t f;
        memset(&f, 0, sizeof f);
        viz_sender_frame(&snd, &f);
        uint8_t m[3] = { 0x90, 50, 100 };
        viz_sender_midi(&snd, m, keep, NULL);
        CHECK(snd.covers == 1, "a Move note drew %ld covers", snd.covers);
        viz_sender_midi(&snd, g_kept + 1, keep, NULL);
        CHECK(snd.covers == 1, "Viz's own cover, echoed, drew another");
        /* at most 4 covers a block (the first note's among them); the rest
         * wait for the re-send */
        for (int k = 0; k < 10; k++) { uint8_t n[3] = { 0x91, (uint8_t)(60 + k), 9 }; viz_sender_midi(&snd, n, keep, NULL); }
        CHECK(snd.covers == 4, "%ld covers in one block, not 4", snd.covers);
        int unknown = 0;
        for (int k = 0; k < 10; k++) { uint8_t n[3] = { 0x91, (uint8_t)(60 + k), 9 }; unknown += !snd.known[viz_grid_painted(GRID_MATRIX, n)]; }
        CHECK(unknown == 10, "only %d of a burst's 10 lights queued to be sent again", unknown);
    }
}

static void test_refusals(void) {
    synth_t s = { 0, 3 };
    void *v = make();
    reset_display(GRID_MATRIX);
    g_refuse_every = 2;                                 /* half of all packets */
    run(v, &s, 6, 1, 1);
    api->set_param(v, "light", "0");
    run(v, &s, 4, 1, 1);
    int lit = 0;
    for (int id = 0; id < NLIGHT; id++) { uint8_t *c = g_disp[viz_grid_index(GRID_MATRIX, id)]; lit += c[0] || c[1] || c[2]; }
    CHECK(lit == 0, "with half the packets refused, %d lights never reached black", lit);
    CHECK(g_bad_msg == 0, "a refused packet broke %d messages", g_bad_msg);
    char buf[32];
    api->get_param(v, "refused", buf, sizeof buf);
    CHECK(atol(buf) > 0, "the fake host refused nothing");
    g_refuse_every = 0;
    api->destroy_instance(v);
}

static void test_turn(void) {
    /* Bars, with only a low sine: the left column is the tall one. A quarter
     * turn clockwise moves it to the top row. */
    for (int turn = 0; turn < 2; turn++) {
        void *v = make();
        reset_display(GRID_MATRIX);
        api->set_param(v, "scene", "Bars");
        api->set_param(v, "edge", "Off");
        api->set_param(v, "turn", turn ? "90" : "0");
        int16_t io[VIZ_BLOCK * 2];
        double ph = 0;
        for (int b = 0; b < (int)(3.0 * VIZ_SR / VIZ_BLOCK); b++) {
            sine_block(&ph, 60, io);
            g_beat = -1;
            g_block++;
            api->process_block(v, io, VIZ_BLOCK);
        }
        /* unturned: the left column is tall and the right one short; turned
         * clockwise, those become the top row and the bottom row */
        double col0 = 0, col7 = 0, row0 = 0, row7 = 0, all = 0;
        for (int i = 0; i < NPAD; i++) {
            uint8_t *c = g_disp[viz_grid_index(GRID_MATRIX, i)];
            double b = c[0] + c[1] + c[2];
            all += b;
            if (i % 8 == 0) col0 += b;
            if (i % 8 == 7) col7 += b;
            if (i / 8 == 0) row0 += b;
            if (i / 8 == 7) row7 += b;
        }
        if (turn == 0) CHECK(col0 > col7 * 1.5, "TURN 0: left column %.0f not brighter than right %.0f", col0, col7);
        else CHECK(row0 > row7 * 1.5, "TURN 90: top row %.0f not brighter than bottom %.0f", row0, row7);
        CHECK(all > 0, "Bars showed nothing for a low sine");
        api->destroy_instance(v);
    }
}

static void test_launchpad(void) {
    synth_t s = { 0, 5 };
    void *v = make();
    api->set_param(v, "grid", "LP X");
    reset_display(GRID_LP_X);
    run(v, &s, 3, 1, 1);
    CHECK(g_bad_msg == 0, "LP X: %d messages did not parse", g_bad_msg);
    CHECK(g_setups >= 1, "LP X was never put in Programmer mode");
    CHECK(g_max_block_pkts <= SEND_PER_BLOCK_MAX, "LP X: %ld packets in a block", g_max_block_pkts);
    int lit = 0;
    for (int i = 0; i < NPAD; i++) { uint8_t *c = g_disp[viz_grid_index(GRID_LP_X, i)]; lit += c[0] || c[1] || c[2]; }
    CHECK(lit > 8, "LP X: only %d pads lit", lit);
    run(v, &s, 4.5, 1, 1);
    CHECK(g_setups >= 2, "LP X: Programmer mode not re-sent (replug)");
    for (int gidx = 0; gidx < GRID_COUNT; gidx++) {
        api->set_param(v, "grid", viz_grid_names[gidx]);
        reset_display(gidx);
        run(v, &s, 1.5, 1, 1);
        CHECK(g_bad_msg == 0, "%s: %d messages did not parse", viz_grid_names[gidx], g_bad_msg);
    }
    api->destroy_instance(v);
}

/* ---- listening -------------------------------------------------------- */

static void test_listening(void) {
    static const double centre[NBAND] = { 60, 120, 250, 500, 1000, 2000, 4000, 8000 };
    int16_t io[VIZ_BLOCK * 2];
    for (int b = 0; b < NBAND; b++) {
        viz_listen_t l;
        viz_features_t f;
        viz_listen_init(&l);
        double ph = 0;
        for (int k = 0; k < (int)(1.0 * VIZ_SR / VIZ_BLOCK); k++) {
            sine_block(&ph, centre[b], io);
            viz_listen_block(&l, io, VIZ_BLOCK, -1, 0, 4, MODE_AUTO, &f);
        }
        int best = 0;
        for (int k = 1; k < NBAND; k++) if (f.band[k] > f.band[best]) best = k;
        CHECK(best == b, "a %.0f Hz sine lit band %d, not %d", centre[b], best, b);
    }

    /* A kick loop: a low hit per kick, no high hits. Then its own beat locks. */
    viz_listen_t l;
    viz_features_t f;
    viz_listen_init(&l);
    synth_t s = { 0, 9 };
    double worst_late = 0;
    int blocks = (int)(12.0 * VIZ_SR / VIZ_BLOCK);
    uint32_t low_before = 0;
    for (int k = 0; k < blocks; k++) {
        music_block(&s, io, 1, 0, 0);
        viz_listen_block(&l, io, VIZ_BLOCK, -1, 0, 4, MODE_AUTO, &f);
        if (k == (int)(2.0 * VIZ_SR / VIZ_BLOCK)) low_before = f.hits[0];
        /* after 4 beats, every kick should land near a whole own-beat */
        if (s.t > 2.0 && fmod(s.t, 0.5) < (double)VIZ_BLOCK / VIZ_SR) {
            double fr = f.beat - floor(f.beat + 0.5);
            if (fabs(fr) > worst_late) worst_late = fabs(fr);
        }
    }
    int kicks = (int)(10.0 / 0.5);
    int low = (int)(f.hits[0] - low_before);
    CHECK(abs(low - kicks) <= 2, "kick loop: %d low hits for %d kicks", low, kicks);
    CHECK(f.hits[2] <= 2, "kick loop: %u high hits", f.hits[2]);
    CHECK(worst_late < 0.12, "own beat: kicks land %.2f beats off", worst_late);
    CHECK(fabs(f.bpm - 120) < 6, "own beat tempo %.1f, not ~120", f.bpm);
    printf("  listening: %d low hits for %d kicks, own beat within %.3f, %.1f BPM\n", low, kicks, worst_late, f.bpm);

    /* MODE Beat ignores the sound. */
    viz_listen_block(&l, io, VIZ_BLOCK, 3.0, 120, 4, MODE_BEAT, &f);
    CHECK(f.level == 0 && f.band[0] == 0 && f.sound == 0, "MODE Beat still reports sound");
    CHECK(f.clock && f.beat == 3.0, "MODE Beat ignores the clock");
    viz_listen_block(&l, io, VIZ_BLOCK, 3.0, 120, 4, MODE_SOUND, &f);
    CHECK(!f.clock, "MODE Sound follows the clock");
    viz_listen_block(&l, io, VIZ_BLOCK, 3.0, 120, 4, MODE_BLEND, &f);
    CHECK(f.clock && f.react_scale == 2.0f, "MODE Blend while playing");
}

/* ---- scenes ----------------------------------------------------------- */

/* Ten minutes of features at 30 frames a second, made once and shared by
 * every scene: music for five minutes with Move playing for the first two,
 * then five of silence. */
#define FEAT_FRAMES (600 * VIZ_FPS)
static viz_features_t *feature_track(void) {
    static viz_features_t *ft;
    if (ft) return ft;
    ft = malloc(sizeof *ft * FEAT_FRAMES);
    int16_t io[VIZ_BLOCK * 2];
    viz_listen_t l;
    viz_features_t f;
    viz_listen_init(&l);
    synth_t s = { 0, 11 };
    int acc = 0, n = 0;
    while (n < FEAT_FRAMES) {
        if (s.t < 300) music_block(&s, io, 1, 1, 1);
        else { memset(io, 0, sizeof io); s.t += (double)VIZ_BLOCK / VIZ_SR; }
        double beat = s.t < 120 ? s.t * 2 : -1;
        viz_listen_block(&l, io, VIZ_BLOCK, beat, 120, 4, MODE_AUTO, &f);
        acc += VIZ_BLOCK;
        if (acc >= VIZ_SR / VIZ_FPS) { acc -= VIZ_SR / VIZ_FPS; ft[n++] = f; }
    }
    return ft;
}

static viz_look_t base_look(int scene) {
    viz_look_t look = { .scene = scene, .k = { 2, 2, 1, 1, 0, 1, 1, 0 }, .light = 0.8f, .edge = EDGEMODE_GLOW,
                        .edge_weight = 2, .edge_sides = 15 };
    return look;
}

static void test_scenes(void) {
    const viz_features_t *ft = feature_track();
    viz_frame_t fr;
    double worst_ms = 0;
    for (int sc = 0; sc < SCENE_COUNT; sc++) {
        viz_look_t look = base_look(sc);
        for (int extreme = 0; extreme < 2; extreme++) {
            if (extreme) look.k = (viz_shape_knobs_t){ 8, 4, 3, 4, 4, 3, 4, 0 };
            viz_render_t *r = viz_render_new(42);
            int black_run = 0, worst_black = 0, nonfinite = 0;
            double ms = 0;
            for (int n = 0; n < FEAT_FRAMES; n++) {
                clock_t c0 = clock();
                viz_render_frame(r, &look, &ft[n], 1.0f / VIZ_FPS, &fr);
                ms += (double)(clock() - c0) * 1000.0 / CLOCKS_PER_SEC;
                int any = 0;
                for (int i = 0; i < NLIGHT; i++) any |= fr.rgb[i][0] | fr.rgb[i][1] | fr.rgb[i][2];
                /* a scene may rest in silence, never while the music plays */
                if (n < 300 * VIZ_FPS) { black_run = any ? 0 : black_run + 1; if (black_run > worst_black) worst_black = black_run; }
                if (sc >= SCENE_ORGANIC) {
                    const float *fld = viz_render_field_rgb(r);
                    for (int i = 0; i < FIELD * FIELD * 3; i += 97) if (!isfinite(fld[i])) nonfinite++;
                }
            }
            CHECK(nonfinite == 0, "%s: %d non-finite field values", viz_scene_names[sc], nonfinite);
            CHECK(worst_black <= 2 * VIZ_FPS, "%s%s: all black for %.1f s while the music plays", viz_scene_names[sc],
                  extreme ? " (all knobs max)" : "", (double)worst_black / VIZ_FPS);
            CHECK(ms / FEAT_FRAMES < 4.0, "%s: %.2f ms a frame on this machine", viz_scene_names[sc], ms / FEAT_FRAMES);
            if (ms / FEAT_FRAMES > worst_ms) worst_ms = ms / FEAT_FRAMES;
            viz_render_free(r);
        }
    }
    printf("  %d scenes, at most %.3f ms a frame\n", SCENE_COUNT, worst_ms);

    /* Every Look knob visibly changes the picture (the user, 2026-10-10: the
     * controls must have a tangible effect). Sweep, 8 s, one knob at a time (Rings would hide FOLD: it is symmetric already). */
    static const int other[7] = { 7, 4, 3, 0, 1, 0, 3 };   /* Mono, 1/16, 4, Off, Sides, Hold, Flash */
    static const char *const kname[7] = { "COLOR", "RATE", "SIZE", "TRAIL", "FOLD", "SHIFT", "REACT" };
    static uint8_t ref[8 * VIZ_FPS][NPAD][3];
    for (int knob = -1; knob < 7; knob++) {
        viz_look_t look = base_look(4);   /* Sweep */
        int *kv = &look.k.color;
        if (knob >= 0) kv[knob] = other[knob];
        viz_render_t *r = viz_render_new(42);
        long differ = 0;
        for (int n = 0; n < 8 * VIZ_FPS; n++) {
            viz_render_frame(r, &look, &ft[n], 1.0f / VIZ_FPS, &fr);
            if (knob < 0) memcpy(ref[n], fr.rgb, sizeof ref[n]);
            else differ += memcmp(ref[n], fr.rgb, sizeof ref[n]) != 0;
        }
        if (knob >= 0) CHECK(differ > 2 * VIZ_FPS, "%s barely changes Sweep: %ld of %d frames differ", kname[knob], differ, 8 * VIZ_FPS);
        viz_render_free(r);
    }

    /* Bit-identical from a fixed seed. */
    static const int seeded[3] = { 3 /* Burst */, 25 /* Stack */, SCENE_FIREFLIES };
    for (int q = 0; q < 3; q++) {
        viz_look_t look = base_look(seeded[q]);
        viz_frame_t a, b;
        viz_render_t *r1 = viz_render_new(7), *r2 = viz_render_new(7);
        int same = 1;
        for (int n = 0; n < 60 * VIZ_FPS; n++) {
            viz_render_frame(r1, &look, &ft[n], 1.0f / VIZ_FPS, &a);
            viz_render_frame(r2, &look, &ft[n], 1.0f / VIZ_FPS, &b);
            if (memcmp(a.rgb, b.rgb, sizeof a.rgb)) same = 0;
        }
        CHECK(same, "%s: two renders from one seed differ", viz_scene_names[look.scene]);
        viz_render_free(r1);
        viz_render_free(r2);
    }
}

/* ---- controls --------------------------------------------------------- */

static void dump(const char *dir, const char *name, const char *text) {
    char p[512];
    snprintf(p, sizeof p, "%s/%s", dir, name);
    FILE *f = fopen(p, "w");
    if (f) { fputs(text, f); fclose(f); }
}

/* ---- the Sequence and Mod pages (seq.c) ---------------------------------- */

static viz_features_t feat_at(double beat) {
    viz_features_t f;
    memset(&f, 0, sizeof f);
    f.beat = beat;
    f.beats_per_bar = 4;
    f.sound = 1;
    return f;
}

static viz_look_t plain_look(void) {
    viz_look_t L;
    memset(&L, 0, sizeof L);
    L.scene = 5;
    L.k = (viz_shape_knobs_t){ 2, 2, 1, 1, 0, 1, 1, 0 };
    L.light = 0.8f;
    return L;
}

static void test_sequence(void) {
    viz_seq_params_t p;
    viz_seq_t s;
    viz_seq_defaults(&p);
    viz_seq_init(&s);

    /* PLAY Off: the look is untouched */
    viz_look_t L = plain_look(), ref = plain_look();
    viz_features_t f = feat_at(3.3);
    viz_seq_apply(&s, &p, &f, &L);
    CHECK(!memcmp(&L, &ref, sizeof L), "PLAY Off changed the look");

    /* step 1 one bar, step 3 two beats of Rings in Fire: 1 3 1 3 ... */
    p.play = 1;
    p.step[2].bars = 3;                       /* 2 beats */
    p.step[2].scene = 1 + 1;                  /* Rings */
    p.step[2].color = 2 + 1;                  /* Fire */
    p.step[2].rate = 2 + 0;                   /* 4 bars */
    static const struct { double beat; int step; } walk[] = {
        { 0, 0 }, { 3.9, 0 }, { 4.0, 2 }, { 5.9, 2 }, { 6.0, 0 }, { 9.99, 0 }, { 10.0, 2 }, { 13, 0 },
        { 1.0, 0 },                           /* Move restarted: back to step 1 */
        { 604, 2 },                           /* jumped ahead: 604 = 100 loops of 6, + 4 */
    };
    for (unsigned i = 0; i < sizeof walk / sizeof walk[0]; i++) {
        L = plain_look();
        f = feat_at(walk[i].beat);
        viz_seq_apply(&s, &p, &f, &L);
        CHECK(s.cur == walk[i].step, "at beat %.2f step %d plays, not %d", walk[i].beat, s.cur + 1, walk[i].step + 1);
        if (s.cur == 2) {
            CHECK(L.scene == 1 && L.k.color == 1, "step 3 shows scene %d colour %d", L.scene, L.k.color);
            CHECK(L.k.steps_per_beat == 1.0 / 16, "step 3's 4 bars is %g steps a beat", L.k.steps_per_beat);
            CHECK(L.k.size == 1 && L.k.mirror == 0, "Same moved size or fold");
        }
    }

    /* Random: the same song position draws the same; passes draw variety */
    viz_seq_defaults(&p);
    p.play = 1;
    p.step[0].bars = 1;                       /* Random length too */
    int seen[SCENE_COUNT] = { 0 }, distinct = 0, lens_ok = 1;
    viz_seq_t a, b;
    viz_seq_init(&a);
    viz_seq_init(&b);
    for (double beat = 0; beat < 2000; beat += 0.5) {
        viz_look_t la = plain_look(), lb = plain_look();
        f = feat_at(beat);
        viz_seq_apply(&a, &p, &f, &la);
        viz_seq_apply(&b, &p, &f, &lb);
        CHECK(la.scene == lb.scene && la.k.color == lb.k.color, "Random drew differently at beat %.1f", beat);
        if (la.scene >= 0 && la.scene < SCENE_COUNT && !seen[la.scene]++) distinct++;
        if (la.k.color < 0 || la.k.color >= SHAPE_COLORS) lens_ok = 0;
        double len = a.len;
        if (!(len == 1 || len == 2 || len == 4 || len == 8 || len == 16 || len == 32)) lens_ok = 0;
    }
    CHECK(distinct > 40, "Random scene drew only %d scenes in 2000 beats", distinct);
    CHECK(lens_ok, "a Random length or colour fell outside its options");

    /* all steps Off: nothing plays */
    for (int i = 0; i < SEQ_STEPS; i++) p.step[i].bars = BARS_OFF;
    L = plain_look();
    f = feat_at(10);
    viz_seq_apply(&s, &p, &f, &L);
    CHECK(s.cur == -1 && L.scene == 5, "with every step Off, step %d plays scene %d", s.cur + 1, L.scene);

    /* the LFOs: depth 0 moves nothing; a Square on Color swings it both ways;
     * Light only ever dims; Kick follows the kicks */
    viz_seq_defaults(&p);
    viz_seq_init(&s);
    L = plain_look();
    f = feat_at(1.3);
    viz_seq_apply(&s, &p, &f, &L);
    CHECK(!memcmp(&L, &ref, sizeof L), "LFOs at depth 0 changed the look");
    p.mod[0] = (viz_mod_t){ MOD_SQUARE, 2, AIM_COLOR, 0.25f };          /* 1/4: a beat a cycle */
    int up = 0, down = 0;
    for (double beat = 0; beat < 8; beat += 0.1) {
        L = plain_look();
        f = feat_at(beat);
        viz_seq_apply(&s, &p, &f, &L);
        up += L.k.color == 4;
        down += L.k.color == 0;
    }
    CHECK(up > 20 && down > 20, "Square on Color: up %d, down %d", up, down);
    p.mod[0] = (viz_mod_t){ MOD_SINE, 4, AIM_LIGHT, 1 };
    float lo = 1, hi = 0;
    for (double beat = 0; beat < 8; beat += 0.1) {
        L = plain_look();
        f = feat_at(beat);
        viz_seq_apply(&s, &p, &f, &L);
        if (L.light < lo) lo = L.light;
        if (L.light > hi) hi = L.light;
    }
    CHECK(hi <= 0.8f + 1e-6f && lo < 0.05f, "Light swung %.2f..%.2f (LIGHT 0.8)", lo, hi);
    p.mod[0] = (viz_mod_t){ MOD_KICK, 2, AIM_SIZE, 1 };
    L = plain_look();
    f = feat_at(20);
    f.hits[0] = 7;                                                    /* a kick lands */
    viz_seq_apply(&s, &p, &f, &L);
    CHECK(L.k.size == 3, "a kick with Kick on Size made size %d", L.k.size);
    L = plain_look();
    f = feat_at(23);
    f.hits[0] = 7;
    viz_seq_apply(&s, &p, &f, &L);
    CHECK(L.k.size == 1, "three beats after the kick, size is %d", L.k.size);
    p.mod[0] = (viz_mod_t){ MOD_RAMP, 0, AIM_TURN, 1 };
    int turns[4] = { 0 };
    for (double beat = 0; beat < 4; beat += 0.01) {
        L = plain_look();
        f = feat_at(beat);
        viz_seq_apply(&s, &p, &f, &L);
        if (L.turn >= 0 && L.turn < 4) turns[L.turn]++;
    }
    CHECK(turns[0] && turns[1] && turns[2] && turns[3], "Ramp on Turn missed a quarter turn");
}

static void test_controls(const char *dir) {
    void *v = make();
    /* The host's buffers: 64 KB for chain_params, 16 KB for the hierarchy, and
     * a sound's state shares the chain_params size; keep well inside. */
    static char cp[65536], ui[16384], st[4096], st2[4096];
    int ncp = api->get_param(v, "chain_params", cp, sizeof cp), nui = api->get_param(v, "ui_hierarchy", ui, sizeof ui);
    CHECK(ncp > 0 && ncp < 32768, "chain_params is %d bytes", ncp);
    CHECK(nui > 0 && nui < 8192, "ui_hierarchy is %d bytes", nui);
    printf("  contract: chain_params %d bytes, ui_hierarchy %d bytes\n", ncp, nui);
    dump(dir, "chain_params.json", cp);
    dump(dir, "ui_hierarchy.json", ui);
    /* every chain_params key is on a page */
    const char *p = cp;
    int keys = 0;
    while ((p = strstr(p, "{\"key\":\""))) {
        p += 8;
        char key[32];
        int n = 0;
        while (*p && *p != '"' && n < 31) key[n++] = *p++;
        key[n] = 0;
        char q[40];
        snprintf(q, sizeof q, "\"%s\"", key);
        CHECK(strstr(ui, q) != NULL, "%s is in chain_params but on no page", key);
        keys++;
    }
    CHECK(keys == 74, "%d keys, expected 74", keys);

    api->set_param(v, "scene", "Lava");
    api->set_param(v, "color", "3");
    api->set_param(v, "rate", "1/16");
    api->set_param(v, "fold", "Eight");
    api->set_param(v, "mode", "Blend");
    api->set_param(v, "nudge", "-75");
    api->set_param(v, "turn", "180");
    api->set_param(v, "grid", "LP Pro MK3");
    api->set_param(v, "nudge", "nonsense");                /* ignored */
    api->set_param(v, "light", "7");                       /* clamped */
    api->set_param(v, "play", "On");
    api->set_param(v, "s3_scene", "Rings");
    api->set_param(v, "s3_bars", "2 beats");
    api->set_param(v, "s5_rate", "4 bars");
    api->set_param(v, "m2_aim", "Turn");
    api->set_param(v, "m2_depth", "-0.5");
    api->set_param(v, "step", "6");                        /* where the page looks: not kept */
    api->get_param(v, "state", st, sizeof st);
    CHECK(strstr(st, "\"step\"") == NULL, "state keeps STEP: %s", st);
    void *w = make();
    api->set_param(w, "state", st);
    api->get_param(w, "state", st2, sizeof st2);
    CHECK(!strcmp(st, st2), "state does not round-trip:\n  %s\n  %s", st, st2);
    char buf[64];
    api->get_param(w, "scene", buf, sizeof buf);  CHECK(!strcmp(buf, "Lava"), "scene %s", buf);
    api->get_param(w, "color", buf, sizeof buf);  CHECK(!strcmp(buf, "Sunset"), "color %s", buf);
    api->get_param(w, "rate", buf, sizeof buf);   CHECK(!strcmp(buf, "1/16"), "rate %s", buf);
    api->get_param(w, "fold", buf, sizeof buf);   CHECK(!strcmp(buf, "Eight"), "fold %s", buf);
    api->get_param(w, "nudge", buf, sizeof buf);  CHECK(!strcmp(buf, "-75"), "nudge %s", buf);
    api->get_param(w, "light", buf, sizeof buf);  CHECK(atof(buf) == 1.0, "light %s", buf);
    api->get_param(w, "grid", buf, sizeof buf);   CHECK(!strcmp(buf, "LP Pro MK3"), "grid %s", buf);
    api->get_param(w, "s3_scene", buf, sizeof buf); CHECK(!strcmp(buf, "Rings"), "s3_scene %s", buf);
    api->get_param(w, "s5_rate", buf, sizeof buf);  CHECK(!strcmp(buf, "4 bars"), "s5_rate %s", buf);
    api->get_param(w, "m2_depth", buf, sizeof buf); CHECK(atof(buf) == -0.5, "m2_depth %s", buf);
    api->get_param(w, "step", buf, sizeof buf);     CHECK(!strcmp(buf, "1"), "step %s", buf);
    /* defaults, as the user set them (2026-10-11) */
    void *d = make();
    const char *want[][2] = { { "play", "Off" }, { "s1_bars", "1 bar" }, { "s2_bars", "Off" }, { "s8_bars", "Off" },
                              { "s1_scene", "Random" }, { "s4_color", "Random" }, { "s4_rate", "Same" },
                              { "s4_size", "Same" }, { "s4_fold", "Same" }, { "m1_depth", "0.00" } };
    for (unsigned i = 0; i < sizeof want / sizeof want[0]; i++) {
        api->get_param(d, want[i][0], buf, sizeof buf);
        CHECK(!strcmp(buf, want[i][1]), "default %s is %s, not %s", want[i][0], buf, want[i][1]);
    }
    api->get_param(d, "seq_now", buf, sizeof buf);
    CHECK(!strcmp(buf, "0|10000000|4|4"), "seq_now at rest is %s", buf);
    api->set_param(d, "play", "On");
    api->set_param(d, "s2_bars", "Random");
    synth_t sy = { 0, 9 };
    run(d, &sy, 0.2, 1, 1);
    api->get_param(d, "seq_now", buf, sizeof buf);
    CHECK(!strcmp(buf, "1|1R000000|0|4"), "seq_now playing is %s", buf);
    api->destroy_instance(d);
    api->destroy_instance(v);
    api->destroy_instance(w);
}

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    const char *dir = argc > 1 ? argv[1] : ".";
    g_host.api_version = 1;
    g_host.sample_rate = VIZ_SR;
    g_host.frames_per_block = VIZ_BLOCK;
    g_host.log = fake_log;
    g_host.midi_send_external = fake_send;
    g_host.get_beat_position = fake_beat;
    g_host.get_bpm = fake_bpm;
    api = move_audio_fx_init_v2(&g_host);

    printf("midi\n");       test_midi();
    printf("move notes\n"); test_move_notes();
    printf("refusals\n");   test_refusals();
    printf("turn\n");       test_turn();
    printf("launchpad\n");  test_launchpad();
    printf("listening\n");  test_listening();
    printf("scenes\n");     test_scenes();
    printf("controls\n");   test_controls(dir);
    printf("sequence\n");   test_sequence();

    if (g_fail) { fprintf(stderr, "test_viz: %d FAILED\n", g_fail); return 1; }
    printf("test_viz: all passed\n");
    return 0;
}
