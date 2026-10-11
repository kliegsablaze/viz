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
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "host/audio_fx_api_v2.h"
#include "host/plugin_api_v1.h"
#include "host/move_info.h"

#include "device.h"
#include "viz.h"

/* ---- the knobs ------------------------------------------------------------ */

enum { PT_ENUM, PT_PCT, PT_INT };
enum {
    P_SCENE, P_COLOR, P_FLOW, P_GROW, P_BLOOM, P_REACT, P_WARP, P_DRIFT,
    P_MODE, P_POWER, P_LIGHT, P_GRID, P_CYCLE, P_TURN, P_EDGE, P_NUDGE, P_COUNT
};

static const char *const k_modes[] = { "Auto", "Beat", "Sound", "Blend" };
static const char *const k_power[] = { "Port", "Hub" };
static const char *const k_cycle[] = { "4", "8", "16", "32", "64" };
static const char *const k_turn[]  = { "0", "90", "180", "270" };
static const char *const k_edge[]  = { "Off", "Glow", "Breathe" };

typedef struct {
    const char *key, *name, *cell;
    int type;
    const char *const *opts;
    int nopts;
    float min, max, def;
    const char *unit;
} pdef_t;

/* name: the held-knob header, spelled out. cell: the knob's own word, at most
 * five letters, a real word (DESIGN.md, Control surface). */
static const pdef_t k_params[P_COUNT] = {
    { "scene", "Scene",            "SCENE", PT_ENUM, viz_scene_names, SCENE_COUNT, 0, 0, 0, NULL },
    { "color", "Colour family",    "COLOR", PT_ENUM, viz_color_names, COLOR_COUNT, 0, 0, 0, NULL },
    { "flow",  "Flow speed",       "FLOW",  PT_PCT,  NULL, 0, 0, 1, 0.5f, NULL },
    { "grow",  "Detail",           "GROW",  PT_PCT,  NULL, 0, 0, 1, 0.5f, NULL },
    { "bloom", "Glow and trails",  "BLOOM", PT_PCT,  NULL, 0, 0, 1, 0.4f, NULL },
    { "react", "Reaction",         "REACT", PT_PCT,  NULL, 0, 0, 1, 0.6f, NULL },
    { "warp",  "Warp",             "WARP",  PT_PCT,  NULL, 0, 0, 1, 0.4f, NULL },
    { "drift", "Evolution speed",  "DRIFT", PT_PCT,  NULL, 0, 0, 1, 0.3f, NULL },
    { "mode",  "Sync mode",        "MODE",  PT_ENUM, k_modes, 4, 0, 0, 0, NULL },
    { "power", "Power source",     "POWER", PT_ENUM, k_power, 2, 0, 0, 0, NULL },
    { "light", "Brightness",       "LIGHT", PT_PCT,  NULL, 0, 0, 1, 0.8f, NULL },
    { "grid",  "Grid device",      "GRID",  PT_ENUM, viz_grid_names, GRID_COUNT, 0, 0, 0, NULL },
    { "cycle", "Journey phrase (bars)", "CYCLE", PT_ENUM, k_cycle, 5, 0, 0, 2, NULL },
    { "turn",  "Rotation (degrees)", "TURN", PT_ENUM, k_turn, 4, 0, 0, 0, NULL },
    { "edge",  "Underglow",        "EDGE",  PT_ENUM, k_edge, 3, 0, 0, 1, NULL },
    { "nudge", "Light timing (ms)", "NUDGE", PT_INT, NULL, 0, -150, 100, -40, "ms" },
};

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
    L->color = pint(s, P_COLOR);
    L->flow = pget(s, P_FLOW);
    L->grow = pget(s, P_GROW);
    L->bloom = pget(s, P_BLOOM);
    L->react = pget(s, P_REACT);
    L->warp = pget(s, P_WARP);
    L->drift = pget(s, P_DRIFT);
    L->light = pget(s, P_LIGHT);
    L->hub = pint(s, P_POWER) == 1;
    L->turn = pint(s, P_TURN);
    L->edge = pint(s, P_EDGE);
    int g = pint(s, P_GRID);
    if (g < 0 || g >= GRID_COUNT) g = 0;
    L->edge_weight = viz_grids[g].edge_weight;
    L->edge_sides = viz_grid_edge_sides(g);
}

static void render_one(viz_t *s, const viz_features_t *f) {
    viz_look_t L;
    build_look(s, &L);
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
    return snprintf(buf, len, "%.3f", (double)pget(s, i));
}

/* state: {"scene":"Aurora","flow":0.500,...}. Parsed by hand: a value is a
 * quoted string or a number. */
static void set_state(viz_t *s, const char *json) {
    for (int i = 0; i < P_COUNT; i++) {
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
    for (int i = 0; i < P_COUNT && pos < len; i++) {
        char v[48];
        get_one(s, i, v, sizeof v);
        const char *q = k_params[i].type == PT_ENUM ? "\"" : "";
        pos += snprintf(buf + pos, len - pos, "%s\"%s\":%s%s%s", i ? "," : "", k_params[i].key, q, v, q);
    }
    if (pos < len) pos += snprintf(buf + pos, len - pos, "}");
    return pos < len ? pos : -1;
}

static int chain_params(char *buf, int len) {
    int pos = snprintf(buf, len, "[");
    for (int i = 0; i < P_COUNT && pos < len; i++) {
        const pdef_t *d = &k_params[i];
        pos += snprintf(buf + pos, len - pos, "%s{\"key\":\"%s\",\"name\":\"%s\",\"short_name\":\"%s\",",
                        i ? "," : "", d->key, d->name, d->cell);
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
        } else {
            pos += snprintf(buf + pos, len - pos, "\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01,"
                            "\"default\":%.2f,\"unit\":\"%%\",\"display_format\":\"%%.0f\"}", (double)d->def);
        }
    }
    if (pos < len) pos += snprintf(buf + pos, len - pos, "]");
    return pos < len ? pos : -1;
}

static int keys_json(char *buf, int len, int from, int to, int with_params) {
    int pos = snprintf(buf, len, "\"knobs\":[");
    for (int i = from; i < to && pos < len; i++)
        pos += snprintf(buf + pos, len - pos, "%s\"%s\"", i > from ? "," : "", k_params[i].key);
    if (pos < len) pos += snprintf(buf + pos, len - pos, "],\"params\":[");
    for (int i = from; i < to && pos < len; i++)
        pos += snprintf(buf + pos, len - pos, "%s{\"key\":\"%s\"}", i > from ? "," : "", k_params[i].key);
    if (with_params && pos < len)
        pos += snprintf(buf + pos, len - pos, ",{\"level\":\"settings\",\"label\":\"Settings\"}");
    if (pos < len) pos += snprintf(buf + pos, len - pos, "]");
    return pos;
}

/* Page 1 (Look) on root, page 2 (Settings) as its own level. */
static int ui_hierarchy(char *buf, int len) {
    char a[1024], b[1024];
    keys_json(a, sizeof a, P_SCENE, P_MODE, 1);
    keys_json(b, sizeof b, P_MODE, P_COUNT, 0);
    int n = snprintf(buf, len, "{\"modes\":null,\"levels\":{\"root\":{\"label\":\"Viz\",%s},"
                               "\"settings\":{\"label\":\"Settings\",%s}}}", a, b);
    return n < len ? n : -1;
}

/* ---- v2 API --------------------------------------------------------------- */

static void *v2_create_instance(const char *module_dir, const char *config_json) {
    (void)module_dir;
    move_info_t mi;
    move_info_read(&mi);          /* the first call resolves the export; off the callback */
    viz_t *s = calloc(1, sizeof *s);
    if (!s) return NULL;
    for (int i = 0; i < P_COUNT; i++) atomic_store(&s->val[i], k_params[i].def);
    viz_listen_init(&s->listen);
    viz_sender_init(&s->sender, GRID_MATRIX);
    s->ts_upper = mi.valid && mi.ts_upper > 0 ? mi.ts_upper : 4;
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
    int i = find_param(key);
    if (i >= 0) return get_one(s, i, buf, len);
    return -1;
}

static void v2_on_midi(void *inst, const uint8_t *msg, int len, int source) {
    (void)inst; (void)msg; (void)len; (void)source;
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
