/*
 * viz.c — Viz as a Schwung Master FX (DESIGN.md).
 *
 * Three threads touch an instance:
 *   - the audio thread (process_block, set_param, get_param): listens,
 *     publishes features through a seqlock, takes the newest frame from a
 *     triple buffer, and sends MIDI. Only it calls midi_send_external
 *     (ext_midi_ring.h: its producers must be on the SPI callback);
 *   - the render thread, started in create_instance (which a Master FX runs on
 *     the loader worker, so the thread is SCHED_OTHER): 30 times a second it
 *     reads the features and the knobs and renders a frame;
 *   - the loader, for create and destroy.
 * The audio thread never allocates, locks, logs or touches a file. Viz never
 * writes the audio buffer.
 */
#define _GNU_SOURCE
#include <math.h>
#include <pthread.h>
#include <sched.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "host/audio_fx_api_v2.h"
#include "host/plugin_api_v1.h"
#include "host/move_info.h"

#include "device.h"
#include "seq.h"
#include "viz.h"

/* ---- the knobs ------------------------------------------------------------ */

enum { PT_ENUM, PT_PCT, PT_INT, PT_BI };
enum {
    P_SCENE, P_COLOR, P_RATE, P_SIZE, P_TRAIL, P_FOLD, P_SHIFT, P_REACT,
    P_MODE, P_POWER, P_LIGHT, P_GRID, P_CYCLE, P_TURN, P_EDGE, P_NUDGE,
    P_STEP, P_PLAY,
    P_SEQ0,                                         /* step i, field f: P_SEQ0 + i * SF_COUNT + f */
    P_MOD0 = P_SEQ0 + SEQ_STEPS * 6,                /* LFO j, field f: P_MOD0 + j * MF_COUNT + f */
    P_COUNT = P_MOD0 + SEQ_MODS * 4
};
enum { SF_SCENE, SF_BARS, SF_COLOR, SF_RATE, SF_SIZE, SF_FOLD, SF_COUNT };
enum { MF_KIND, MF_RATE, MF_AIM, MF_DEPTH, MF_COUNT };
#define P_BASE (P_NUDGE + 1)    /* the two pages that came first */

static const char *const k_modes[] = { "Auto", "Beat", "Sound", "Blend" };
static const char *const k_power[] = { "Port", "Hub" };
static const char *const k_cycle[] = { "4", "8", "16", "32", "64" };
static const char *const k_turn[]  = { "0", "90", "180", "270" };
static const char *const k_edge[]  = { "Off", "Glow", "Breathe" };
static const char *const k_steps[] = { "1", "2", "3", "4", "5", "6", "7", "8" };
static const char *const k_onoff[] = { "Off", "On" };
static const char *k_step_scenes[SEQ_SCENE_OPTS];   /* Random, then every scene: filled in init_params */

typedef struct {
    const char *key, *name, *cell;
    int type;
    const char *const *opts;
    int nopts;
    float min, max, def;
    const char *unit;
    int page;                   /* 0 Look, 1 Settings, 2 Sequence, 3 Mod */
} pdef_t;

/* name: the held-knob header, spelled out. cell: the knob's own word, at most
 * five letters, a real word (DESIGN.md, Control surface). */
static pdef_t k_params[P_COUNT] = {
    { "scene",  "Scene",             "SCENE", PT_ENUM, viz_scene_names, SCENE_COUNT, 0, 0, 0, NULL, 0 },
    { "color",  "Colour set",        "COLOR", PT_ENUM, viz_shape_color_names, SHAPE_COLORS, 0, 0, 2, NULL, 0 },
    { "rate",   "Steps, on the beat", "RATE", PT_ENUM, viz_shape_rate_names, SHAPE_RATES, 0, 0, 2, NULL, 0 },
    { "size",   "Shape size (pads)", "SIZE",  PT_ENUM, viz_shape_size_names, SHAPE_SIZES, 0, 0, 1, NULL, 0 },
    { "trail",  "Trail length",      "TRAIL", PT_ENUM, viz_shape_trail_names, SHAPE_TRAILS, 0, 0, 1, NULL, 0 },
    { "fold",   "Mirror fold",       "FOLD",  PT_ENUM, viz_shape_mirror_names, SHAPE_MIRRORS, 0, 0, 0, NULL, 0 },
    { "shift",  "Colour swap",       "SHIFT", PT_ENUM, viz_shape_shift_names, SHAPE_SHIFTS, 0, 0, 1, NULL, 0 },
    { "react",  "Kick reaction",     "REACT", PT_ENUM, viz_shape_react_names, SHAPE_REACTS, 0, 0, 1, NULL, 0 },
    { "mode",  "Sync mode",        "MODE",  PT_ENUM, k_modes, 4, 0, 0, 0, NULL, 1 },
    { "power", "Power source",     "POWER", PT_ENUM, k_power, 2, 0, 0, 0, NULL, 1 },
    { "light", "Brightness",       "LIGHT", PT_PCT,  NULL, 0, 0, 1, 0.8f, NULL, 1 },
    { "grid",  "Grid device",      "GRID",  PT_ENUM, viz_grid_names, GRID_COUNT, 0, 0, 0, NULL, 1 },
    { "cycle", "Journey phrase (bars)", "CYCLE", PT_ENUM, k_cycle, 5, 0, 0, 2, NULL, 1 },
    { "turn",  "Rotation (degrees)", "TURN", PT_ENUM, k_turn, 4, 0, 0, 0, NULL, 1 },
    { "edge",  "Underglow",        "EDGE",  PT_ENUM, k_edge, 3, 0, 0, 1, NULL, 1 },
    { "nudge", "Light timing (ms)", "NUDGE", PT_INT, NULL, 0, -150, 100, -40, "ms", 1 },
    { "step",  "Step to edit",     "STEP",  PT_ENUM, k_steps, SEQ_STEPS, 0, 0, 0, NULL, 2 },
    { "play",  "Play the sequence", "PLAY", PT_ENUM, k_onoff, 2, 0, 0, 0, NULL, 2 },
};

/* The Sequence page's 48 step knobs and the Mod page's 8, named in place:
 * "Step 3 scene", "LFO 2 depth". Called before anything reads the table. */
static char k_names[P_COUNT][32];
static char k_keys[P_COUNT][16];
static void init_params(void) {
    static int done;
    if (done) return;
    done = 1;
    k_step_scenes[0] = "Random";
    for (int i = 0; i < SCENE_COUNT; i++) k_step_scenes[1 + i] = viz_scene_names[i];
    k_params[P_SCENE].opts = viz_scene_names;
    viz_seq_params_t d;
    viz_seq_defaults(&d);
    static const struct { const char *key, *name, *cell; const char *const *opts; int n; } sf[SF_COUNT] = {
        { "scene", "scene",             "SCENE", k_step_scenes,      SEQ_SCENE_OPTS },
        { "bars",  "length",            "BARS",  viz_seq_bars_names, SEQ_BARS_OPTS },
        { "color", "colour set",        "COLOR", viz_seq_color_names, SEQ_COLOR_OPTS },
        { "rate",  "steps, on the beat", "RATE", viz_seq_rate_names, SEQ_RATE_OPTS },
        { "size",  "shape size",        "SIZE",  viz_seq_size_names, SEQ_SIZE_OPTS },
        { "fold",  "mirror fold",       "FOLD",  viz_seq_fold_names, SEQ_FOLD_OPTS },
    };
    for (int i = 0; i < SEQ_STEPS; i++)
        for (int f = 0; f < SF_COUNT; f++) {
            int ix = P_SEQ0 + i * SF_COUNT + f;
            const int *dv = &d.step[i].scene;
            snprintf(k_keys[ix], sizeof k_keys[ix], "s%d_%s", i + 1, sf[f].key);
            snprintf(k_names[ix], sizeof k_names[ix], "Step %d %s", i + 1, sf[f].name);
            k_params[ix] = (pdef_t){ k_keys[ix], k_names[ix], sf[f].cell, PT_ENUM, sf[f].opts, sf[f].n, 0, 0, (float)dv[f], NULL, 2 };
        }
    static const struct { const char *key, *name, *cell; const char *const *opts; int n; int type; } mf[MF_COUNT] = {
        { "kind",  "shape",  "KIND",  viz_mod_kind_names, MOD_KINDS, PT_ENUM },
        { "rate",  "speed",  "RATE",  viz_mod_rate_names, MOD_RATES, PT_ENUM },
        { "aim",   "target", "AIM",   viz_mod_aim_names,  MOD_AIMS,  PT_ENUM },
        { "depth", "depth",  "DEPTH", NULL, 0, PT_BI },
    };
    for (int j = 0; j < SEQ_MODS; j++)
        for (int f = 0; f < MF_COUNT; f++) {
            int ix = P_MOD0 + j * MF_COUNT + f;
            float dv = f == MF_KIND ? d.mod[j].kind : f == MF_RATE ? d.mod[j].rate : f == MF_AIM ? d.mod[j].aim : d.mod[j].depth;
            snprintf(k_keys[ix], sizeof k_keys[ix], "m%d_%s", j + 1, mf[f].key);
            snprintf(k_names[ix], sizeof k_names[ix], "LFO %d %s", j + 1, mf[f].name);
            k_params[ix] = (pdef_t){ k_keys[ix], k_names[ix], mf[f].cell, mf[f].type, mf[f].opts, mf[f].n,
                                     f == MF_DEPTH ? -1 : 0, f == MF_DEPTH ? 1 : 0, dv, NULL, 3 };
        }
}

/* ---- the instance --------------------------------------------------------- */

typedef struct {
    _Atomic float val[P_COUNT];

    /* audio thread */
    viz_listen_t listen;
    viz_features_t feat;
    viz_sender_t sender;
    int front;
    int ts_upper;

    /* audio -> render: features, by seqlock */
    atomic_uint fseq;
    viz_features_t fpub;

    /* render -> audio: frames, by triple buffer. mid holds an index, plus
     * FRESH when the render side has published since the audio side took. */
    viz_frame_t frames[3];
    atomic_int mid;
    int back;

    viz_render_t *r;
    viz_seq_t seq;              /* render thread */
    atomic_int seq_cur;         /* the step playing, for the STEP picture: -1 none */
    pthread_t thread;
    int threaded;
    atomic_int quit;
    int sync_acc;               /* tests: render in step with the audio instead */
} viz_t;

#define FRESH 4

static const host_api_v1_t *g_host;
static audio_fx_api_v2_t g_api;

static int send_pkt(void *ctx, const uint8_t p[4]) {
    (void)ctx;
    return g_host && g_host->midi_send_external ? g_host->midi_send_external(p, 4) : 0;
}

static float pget(viz_t *s, int i) { return atomic_load_explicit(&s->val[i], memory_order_relaxed); }
static int   pint(viz_t *s, int i) { return (int)lrintf(pget(s, i)); }

static void publish_features(viz_t *s) {
    unsigned q = atomic_load_explicit(&s->fseq, memory_order_relaxed);
    atomic_store_explicit(&s->fseq, q + 1, memory_order_relaxed);
    atomic_thread_fence(memory_order_release);
    memcpy(&s->fpub, &s->feat, sizeof s->feat);
    atomic_thread_fence(memory_order_release);
    atomic_store_explicit(&s->fseq, q + 2, memory_order_relaxed);
}

static void read_features(viz_t *s, viz_features_t *out) {
    for (;;) {
        unsigned a = atomic_load_explicit(&s->fseq, memory_order_acquire);
        if (a & 1) { sched_yield(); continue; }
        memcpy(out, &s->fpub, sizeof *out);
        atomic_thread_fence(memory_order_acquire);
        if (atomic_load_explicit(&s->fseq, memory_order_relaxed) == a) return;
    }
}

static void build_look(viz_t *s, viz_look_t *L) {
    memset(L, 0, sizeof *L);
    L->scene = pint(s, P_SCENE);
    L->k.color = pint(s, P_COLOR);
    L->k.rate = pint(s, P_RATE);
    L->k.size = pint(s, P_SIZE);
    L->k.trail = pint(s, P_TRAIL);
    L->k.mirror = pint(s, P_FOLD);
    L->k.shift = pint(s, P_SHIFT);
    L->k.react = pint(s, P_REACT);
    L->light = pget(s, P_LIGHT);
    L->hub = pint(s, P_POWER) == 1;
    L->turn = pint(s, P_TURN);
    L->edge = pint(s, P_EDGE);
    int g = pint(s, P_GRID);
    if (g < 0 || g >= GRID_COUNT) g = 0;
    L->edge_weight = viz_grids[g].edge_weight;
    L->edge_sides = viz_grid_edge_sides(g);
}

static void build_seq(viz_t *s, viz_seq_params_t *p) {
    p->play = pint(s, P_PLAY);
    for (int i = 0; i < SEQ_STEPS; i++) {
        int *f = &p->step[i].scene;
        for (int k = 0; k < SF_COUNT; k++) f[k] = pint(s, P_SEQ0 + i * SF_COUNT + k);
    }
    for (int j = 0; j < SEQ_MODS; j++) {
        int b = P_MOD0 + j * MF_COUNT;
        p->mod[j] = (viz_mod_t){ pint(s, b + MF_KIND), pint(s, b + MF_RATE), pint(s, b + MF_AIM), pget(s, b + MF_DEPTH) };
    }
}

static void render_one(viz_t *s, const viz_features_t *f) {
    viz_look_t L;
    viz_seq_params_t sp;
    build_look(s, &L);
    build_seq(s, &sp);
    viz_seq_apply(&s->seq, &sp, f, &L);
    atomic_store_explicit(&s->seq_cur, s->seq.cur, memory_order_relaxed);
    viz_render_frame(s->r, &L, f, 1.0f / VIZ_FPS, &s->frames[s->back]);
    s->back = atomic_exchange(&s->mid, s->back | FRESH) & 3;
}

static void *render_main(void *arg) {
    viz_t *s = arg;
    struct timespec next;
    clock_gettime(CLOCK_MONOTONIC, &next);
    viz_features_t f;
    while (!atomic_load(&s->quit)) {
        read_features(s, &f);
        render_one(s, &f);
        next.tv_nsec += 1000000000L / VIZ_FPS;
        if (next.tv_nsec >= 1000000000L) { next.tv_nsec -= 1000000000L; next.tv_sec++; }
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        if (now.tv_sec > next.tv_sec + 1) next = now;   /* fell far behind: don't race to catch up */
#ifdef __APPLE__
        /* macOS has no clock_nanosleep; only the tests build here, and they render in step */
        long ns = (next.tv_sec - now.tv_sec) * 1000000000L + (next.tv_nsec - now.tv_nsec);
        if (ns > 0) { struct timespec d = { ns / 1000000000L, ns % 1000000000L }; nanosleep(&d, NULL); }
#else
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL);
#endif
    }
    return NULL;
}

/* ---- params --------------------------------------------------------------- */

static int find_param(const char *key) {
    for (int i = 0; i < P_COUNT; i++) if (!strcmp(key, k_params[i].key)) return i;
    return -1;
}

static void set_one(viz_t *s, int i, const char *val) {
    const pdef_t *d = &k_params[i];
    float v;
    if (d->type == PT_ENUM) {
        int idx = -1;
        for (int k = 0; k < d->nopts; k++) if (!strcmp(val, d->opts[k])) idx = k;
        if (idx < 0) {
            char *end;
            long n = strtol(val, &end, 10);
            if (end == val) return;
            idx = (int)n;
        }
        if (idx < 0) idx = 0;
        if (idx >= d->nopts) idx = d->nopts - 1;
        v = (float)idx;
    } else {
        char *end;
        v = strtof(val, &end);
        if (end == val || !isfinite(v)) return;
        if (d->type == PT_INT) v = roundf(v);
        if (v < d->min) v = d->min;
        if (v > d->max) v = d->max;
    }
    atomic_store_explicit(&s->val[i], v, memory_order_relaxed);
}

static int get_one(viz_t *s, int i, char *buf, int len) {
    const pdef_t *d = &k_params[i];
    if (d->type == PT_ENUM) {
        int idx = pint(s, i);
        if (idx < 0 || idx >= d->nopts) idx = 0;
        return snprintf(buf, len, "%s", d->opts[idx]);
    }
    if (d->type == PT_INT) return snprintf(buf, len, "%d", pint(s, i));
    if (d->type == PT_BI) return snprintf(buf, len, "%.2f", (double)pget(s, i));
    return snprintf(buf, len, "%.3f", (double)pget(s, i));
}

/* state: {"scene":"Rings","color":"Acid",...}. Parsed by hand: a value is a
 * quoted string or a number. STEP is where the Sequence page is looking, not
 * part of the show, so it is not kept. */
static void set_state(viz_t *s, const char *json) {
    for (int i = 0; i < P_COUNT; i++) {
        if (i == P_STEP) continue;
        char pat[32];
        snprintf(pat, sizeof pat, "\"%s\"", k_params[i].key);
        const char *p = strstr(json, pat);
        if (!p) continue;
        p += strlen(pat);
        while (*p == ' ' || *p == ':') p++;
        char v[48];
        int n = 0;
        if (*p == '"') {
            p++;
            while (*p && *p != '"' && n < (int)sizeof v - 1) v[n++] = *p++;
        } else {
            while (*p && *p != ',' && *p != '}' && n < (int)sizeof v - 1) v[n++] = *p++;
        }
        v[n] = 0;
        set_one(s, i, v);
    }
}

static int get_state(viz_t *s, char *buf, int len) {
    int pos = snprintf(buf, len, "{");
    for (int i = 0, first = 1; i < P_COUNT && pos < len; i++) {
        if (i == P_STEP) continue;
        char v[48];
        get_one(s, i, v, sizeof v);
        const char *q = k_params[i].type == PT_ENUM ? "\"" : "";
        pos += snprintf(buf + pos, len - pos, "%s\"%s\":%s%s%s", first ? "" : ",", k_params[i].key, q, v, q);
        first = 0;
    }
    if (pos < len) pos += snprintf(buf + pos, len - pos, "}");
    return pos < len ? pos : -1;
}

/* The STEP and PLAY pictures need the whole sequence, not one knob: which
 * steps are on, which is playing, how long the loop is. One read-only key
 * carries it ("seq_now", the pictures' extra key): "playing|steps|beats|bar",
 * playing 1..8 or 0, steps a character each (0 Off, 1 on, R Random length),
 * the loop's beats (0 when a length is Random). */
static int seq_now(viz_t *s, char *buf, int len) {
    char st[SEQ_STEPS + 1];
    double beats = 0;
    int any_random = 0;
    for (int i = 0; i < SEQ_STEPS; i++) {
        int b = pint(s, P_SEQ0 + i * SF_COUNT + SF_BARS);
        st[i] = b == BARS_OFF ? '0' : b == BARS_RANDOM ? 'R' : '1';
        if (b == BARS_RANDOM) any_random = 1;
        beats += viz_seq_bars_beats(b, s->ts_upper);
    }
    st[SEQ_STEPS] = 0;
    int cur = pint(s, P_PLAY) ? atomic_load_explicit(&s->seq_cur, memory_order_relaxed) + 1 : 0;
    return snprintf(buf, len, "%d|%s|%g|%d", cur < 0 ? 0 : cur, st, any_random ? 0.0 : beats, s->ts_upper);
}

static int chain_params(char *buf, int len) {
    int pos = snprintf(buf, len, "[");
    for (int i = 0; i < P_COUNT && pos < len; i++) {
        const pdef_t *d = &k_params[i];
        pos += snprintf(buf + pos, len - pos, "%s{\"key\":\"%s\",\"name\":\"%s\",\"short_name\":\"%s\",",
                        i ? "," : "", d->key, d->name, d->cell);
        /* Every knob but Settings' draws its own picture (src/canvas.js). */
        if (d->page != 1 && pos < len)
            pos += snprintf(buf + pos, len - pos, "\"viz\":{\"kind\":\"custom:viz\"%s},",
                            i == P_STEP || i == P_PLAY ? ",\"extra_keys\":[\"seq_now\"]" : "");
        if (pos >= len) break;
        if (d->type == PT_ENUM) {
            pos += snprintf(buf + pos, len - pos, "\"type\":\"enum\",\"options\":[");
            for (int k = 0; k < d->nopts && pos < len; k++)
                pos += snprintf(buf + pos, len - pos, "%s\"%s\"", k ? "," : "", d->opts[k]);
            if (pos < len)
                pos += snprintf(buf + pos, len - pos, "],\"options_as_string\":true,\"default\":\"%s\"}",
                                d->opts[(int)d->def]);
        } else if (d->type == PT_INT) {
            pos += snprintf(buf + pos, len - pos, "\"type\":\"int\",\"min\":%d,\"max\":%d,\"default\":%d,"
                            "\"unit\":\"%s\"}", (int)d->min, (int)d->max, (int)d->def, d->unit);
        } else if (d->type == PT_BI) {
            pos += snprintf(buf + pos, len - pos, "\"type\":\"float\",\"min\":-1,\"max\":1,\"step\":0.02,"
                            "\"default\":%.2f}", (double)d->def);
        } else {
            pos += snprintf(buf + pos, len - pos, "\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01,"
                            "\"default\":%.2f,\"unit\":\"%%\",\"display_format\":\"%%.0f\"}", (double)d->def);
        }
    }
    if (pos < len) pos += snprintf(buf + pos, len - pos, "]");
    return pos < len ? pos : -1;
}

/* One level: its knobs in order, each step knob gated on STEP. */
typedef struct { char *buf; int len, pos; } sb_t;
static void sb(sb_t *b, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void sb(sb_t *b, const char *fmt, ...) {
    if (b->pos >= b->len) return;
    va_list ap;
    va_start(ap, fmt);
    b->pos += vsnprintf(b->buf + b->pos, b->len - b->pos, fmt, ap);
    va_end(ap);
}
static void level(sb_t *b, const char *name, const char *label, const int *keys, int n, int links) {
    sb(b, "\"%s\":{\"label\":\"%s\",\"knobs\":[", name, label);
    for (int i = 0; i < n; i++) sb(b, "%s\"%s\"", i ? "," : "", k_params[keys[i]].key);
    sb(b, "],\"params\":[");
    for (int i = 0; i < n; i++) {
        int k = keys[i];
        sb(b, "%s{\"key\":\"%s\"", i ? "," : "", k_params[k].key);
        if (k >= P_SEQ0 && k < P_MOD0)
            sb(b, ",\"visible_if\":{\"param\":\"step\",\"equals\":\"%d\"}", (k - P_SEQ0) / SF_COUNT + 1);
        sb(b, "}");
    }
    if (links)
        sb(b, ",{\"level\":\"settings\",\"label\":\"Settings\"},{\"level\":\"sequence\",\"label\":\"Sequence\"},"
              "{\"level\":\"mod\",\"label\":\"Mod\"}");
    sb(b, "]}");
}

/* Look on root, then Settings, Sequence and Mod, in the order the jog walks
 * them. The Sequence page lists every step's knobs; the gates leave the
 * chosen step's, so it reads STEP SCENE BARS PLAY over COLOR RATE SIZE FOLD. */
static int ui_hierarchy(char *buf, int len) {
    int look[8], set[8], seq[2 + SEQ_STEPS * SF_COUNT], mod[SEQ_MODS * MF_COUNT], n = 0;
    for (int i = 0; i < 8; i++) { look[i] = P_SCENE + i; set[i] = P_MODE + i; }
    seq[n++] = P_STEP;
    for (int f = SF_SCENE; f <= SF_BARS; f++)
        for (int i = 0; i < SEQ_STEPS; i++) seq[n++] = P_SEQ0 + i * SF_COUNT + f;   /* in pairs: s1_scene .. s8_scene, s1_bars .. */
    seq[n++] = P_PLAY;
    for (int f = SF_COLOR; f < SF_COUNT; f++)
        for (int i = 0; i < SEQ_STEPS; i++) seq[n++] = P_SEQ0 + i * SF_COUNT + f;
    for (int i = 0; i < SEQ_MODS * MF_COUNT; i++) mod[i] = P_MOD0 + i;
    sb_t b = { buf, len, 0 };
    sb(&b, "{\"modes\":null,\"levels\":{");
    level(&b, "root", "Viz", look, 8, 1);
    sb(&b, ",");
    level(&b, "settings", "Settings", set, 8, 0);
    sb(&b, ",");
    level(&b, "sequence", "Sequence", seq, n, 0);
    sb(&b, ",");
    level(&b, "mod", "Mod", mod, SEQ_MODS * MF_COUNT, 0);
    sb(&b, "}}");
    return b.pos < len ? b.pos : -1;
}

/* ---- v2 API --------------------------------------------------------------- */

static void *v2_create_instance(const char *module_dir, const char *config_json) {
    (void)module_dir;
    init_params();
    move_info_t mi;
    move_info_read(&mi);          /* the first call resolves the export; off the callback */
    viz_t *s = calloc(1, sizeof *s);
    if (!s) return NULL;
    for (int i = 0; i < P_COUNT; i++) atomic_store(&s->val[i], k_params[i].def);
    viz_listen_init(&s->listen);
    viz_sender_init(&s->sender, GRID_MATRIX);
    s->ts_upper = mi.valid && mi.ts_upper > 0 ? mi.ts_upper : 4;
    viz_seq_init(&s->seq);
    atomic_store(&s->seq_cur, -1);
    s->r = viz_render_new(0x5EED1234u);
    if (!s->r) { free(s); return NULL; }
    s->front = 0;
    atomic_store(&s->mid, 1);
    s->back = 2;
    s->feat.beats_per_bar = 4;
    s->feat.bpm = 120;
    publish_features(s);

    int sync = config_json && strstr(config_json, "\"sync_render\"");
    if (!sync) {
        pthread_attr_t a;
        pthread_attr_init(&a);
        pthread_attr_setinheritsched(&a, PTHREAD_EXPLICIT_SCHED);
        pthread_attr_setschedpolicy(&a, SCHED_OTHER);
        struct sched_param sp = { 0 };
        pthread_attr_setschedparam(&a, &sp);
        s->threaded = pthread_create(&s->thread, &a, render_main, s) == 0;
        pthread_attr_destroy(&a);
    }
    if (g_host && g_host->log) g_host->log(s->threaded ? "viz: render thread started" : "viz: rendering in step");
    return s;
}

static void v2_destroy_instance(void *inst) {
    viz_t *s = inst;
    if (!s) return;
    atomic_store(&s->quit, 1);
    if (s->threaded) pthread_join(s->thread, NULL);
    viz_render_free(s->r);
    free(s);
}

static void v2_process_block(void *inst, int16_t *io, int frames) {
    viz_t *s = inst;
    move_info_t mi;
    if (move_info_read(&mi) && mi.valid && mi.ts_upper > 0) s->ts_upper = mi.ts_upper;
    double beat = g_host && g_host->get_beat_position ? g_host->get_beat_position() : -1.0;
    float bpm = g_host && g_host->get_bpm ? g_host->get_bpm() : 0.0f;
    if (beat >= 0) {
        /* NUDGE: the lights earlier (negative) or later than the clock says */
        double b = beat + pget(s, P_NUDGE) / 1000.0 * (bpm > 20 ? bpm : 120) / 60.0;
        beat = b < 0 ? 0 : b;
    }
    viz_listen_block(&s->listen, io, frames, beat, bpm, s->ts_upper, pint(s, P_MODE), &s->feat);
    publish_features(s);

    if (!s->threaded) {               /* tests only: one frame every 1/30 s of audio */
        s->sync_acc += frames;
        while (s->sync_acc >= VIZ_SR / VIZ_FPS) {
            s->sync_acc -= VIZ_SR / VIZ_FPS;
            render_one(s, &s->feat);
        }
    }

    if (atomic_load(&s->mid) & FRESH) {
        s->front = atomic_exchange(&s->mid, s->front) & 3;
        viz_sender_frame(&s->sender, &s->frames[s->front]);
    }
    int g = pint(s, P_GRID);
    if (g != s->sender.grid) viz_sender_set_grid(&s->sender, g);
    viz_sender_block(&s->sender, send_pkt, NULL);
}

static void v2_set_param(void *inst, const char *key, const char *val) {
    viz_t *s = inst;
    if (!s || !key || !val) return;
    if (!strcmp(key, "state")) { set_state(s, val); return; }
    int i = find_param(key);
    if (i >= 0) set_one(s, i, val);
}

static int v2_get_param(void *inst, const char *key, char *buf, int len) {
    viz_t *s = inst;
    if (!s || !key || !buf || len <= 0) return -1;
    if (!strcmp(key, "state")) return get_state(s, buf, len);
    if (!strcmp(key, "chain_params")) return chain_params(buf, len);
    if (!strcmp(key, "ui_hierarchy")) return ui_hierarchy(buf, len);
    /* diagnostics, for tests and logs */
    if (!strcmp(key, "sent"))    return snprintf(buf, len, "%ld", s->sender.sent);
    if (!strcmp(key, "refused")) return snprintf(buf, len, "%ld", s->sender.refused);
    if (!strcmp(key, "seq_now")) return seq_now(s, buf, len);
    int i = find_param(key);
    if (i >= 0) return get_one(s, i, buf, len);
    return -1;
}

static void v2_on_midi(void *inst, const uint8_t *msg, int len, int source) {
    (void)inst; (void)msg; (void)len; (void)source;
}

audio_fx_api_v2_t *move_audio_fx_init_v2(const host_api_v1_t *host) {
    g_host = host;
    init_params();
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
