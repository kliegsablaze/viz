/*
 * viz.h — what the pieces of Viz share (DESIGN.md, "The light engine").
 *
 *   features.c  listening: bands, hits, the automatic level, time (audio thread)
 *   render.c    pictures: scenes, palettes, bloom, edge, LIGHT, power guard
 *   scenes.c    the scenes themselves
 *   device.c    grids: turning a finished frame into SysEx, within budget
 *   viz.c       the Schwung plugin: params, threads, hand-offs
 *
 * features.c, render.c and scenes.c never touch the host, so the preview tool
 * (tools/preview) links them unchanged on a Mac.
 */
#ifndef VIZ_H
#define VIZ_H

#include <stdint.h>

#define VIZ_SR      44100
#define VIZ_BLOCK   128
#define VIZ_FPS     30
#define NPAD        64
#define NEDGE       32          /* 8 per side: right, left, bottom, top */
#define NLIGHT      (NPAD + NEDGE)
#define FIELD       32          /* organic scenes draw 32x32, 4x4 samples per pad */
#define NBAND       8

/* Edge positions: right and left run top to bottom, bottom and top run left to
 * right. Light id = NPAD + side * 8 + i. */
enum { EDGE_RIGHT, EDGE_LEFT, EDGE_BOTTOM, EDGE_TOP };

/* ---- what the music is doing (audio thread -> render thread) ------------ */

typedef struct {
    float    band[NBAND];   /* 0..1, after the automatic level */
    float    level;         /* 0..1 overall loudness */
    float    bright;        /* 0..1 high bands against low */
    float    hit[3];        /* 0..1 decaying pulses: low (kick), mid, high (hats) */
    uint32_t hits[3];       /* how many of each so far; scenes act on the change */
    double   beat;          /* beats, continuous: the clock's, or Viz's own */
    float    bpm;
    int      beats_per_bar;
    int      clock;         /* time comes from Move's clock */
    int      sound;         /* 0: sound is ignored (MODE Beat) */
    float    react_scale;   /* 2 in Blend while playing, else 1 */
    float    silence;       /* seconds since the mix was last audible */
    double   seconds;       /* audio time since Viz started */
} viz_features_t;

enum { MODE_AUTO, MODE_BEAT, MODE_SOUND, MODE_BLEND, MODE_COUNT };

typedef struct {
    /* filters (features.c) */
    float b0[NBAND], b1[NBAND], b2[NBAND], a1[NBAND], a2[NBAND];
    float z1[NBAND], z2[NBAND];
    float env[NBAND];       /* band rms, attack/release */
    float ref;              /* the automatic level's reference */
    float grp_slow[3];      /* slow log energy per hit group */
    float grp_lin[3];       /* slow linear energy per hit group */
    float cand[3];          /* a mid or high hit waiting one block for the lows */
    float flux_mean[3];
    float hold[3];          /* seconds left of each hit's refractory time */
    float hit[3];
    uint32_t hits[3];
    float silence;
    double seconds;
    /* Viz's own beat, for when the clock is not driving */
    double own_beat;
    float  own_period;      /* seconds per beat */
    double last_low_hit;
    int    was_clock;
} viz_listen_t;

void viz_listen_init(viz_listen_t *l);
/* One block of stereo int16. clock_beat < 0 means Move is stopped. */
void viz_listen_block(viz_listen_t *l, const int16_t *io, int frames,
                      double clock_beat, float bpm, int beats_per_bar,
                      int mode, viz_features_t *out);

/* ---- the knobs, as the renderer sees them -------------------------------- */

enum { SCENE_AURORA, SCENE_NEBULA, SCENE_TIDE, SCENE_FIREFLIES, SCENE_REEDS, SCENE_LAVA,
       SCENE_BARS, SCENE_PULSE, SCENE_SCANNER, SCENE_COUNT };
#define COLOR_COUNT 10
enum { EDGEMODE_OFF, EDGEMODE_GLOW, EDGEMODE_BREATHE };

typedef struct {
    int   scene, color;
    float flow, grow, bloom, react, warp, drift;   /* 0..1 */
    float light;                                   /* 0..1 */
    int   hub;                                     /* POWER Hub: no guard */
    int   turn;                                    /* quarter turns clockwise */
    int   edge;
    float edge_weight;                             /* the grid's edge cost, for the guard */
    unsigned edge_sides;                           /* bit per EDGE_* side the grid has */
} viz_look_t;

extern const char *const viz_scene_names[SCENE_COUNT];
extern const char *const viz_color_names[COLOR_COUNT];

/* ---- a finished frame (render thread -> audio thread) ------------------- */

typedef struct {
    uint8_t rgb[NLIGHT][3];  /* 8-bit, after LIGHT and the power guard */
    uint32_t seq;
} viz_frame_t;

#define POWER_BUDGET 33000.0f  /* sum of 8-bit R+G+B, edge weighted; probe/RESULTS.md */

typedef struct viz_render viz_render_t;

viz_render_t *viz_render_new(uint32_t seed);
void viz_render_free(viz_render_t *r);
/* One frame, dt seconds after the last. Also exposes the 32x32 picture the
 * pads came from, for the preview (NULL-safe). */
void viz_render_frame(viz_render_t *r, const viz_look_t *look, const viz_features_t *f,
                      float dt, viz_frame_t *out);
const float *viz_render_field_rgb(const viz_render_t *r);   /* FIELD*FIELD*3, 0..1 */

/* ---- scenes (scenes.c) ---------------------------------------------------- */

typedef struct {
    /* in */
    const viz_features_t *f;
    const viz_look_t *look;
    float t;            /* scene time: seconds, scaled by FLOW and tempo */
    float dt;           /* scene time step */
    float rdt;          /* real time step */
    float react;        /* REACT, times the mode's scale, 0 when sound is ignored */
    int   new_hits[3];  /* hits since the last frame */
    int   beat_tick;    /* a beat began this frame */
    int   bar_tick;
    float beat_phase;   /* 0..1 */
    float bar_phase;
    uint32_t *rng;
    /* out: organic scenes fill FIELD*FIELD, graphic ones 8*8 */
    float *inten;       /* brightness, 0..1 (may exceed) */
    float *coord;       /* where in the colour gradient, any real */
} viz_scene_io_t;

#define SCENE_STATE_BYTES 16384

typedef struct {
    const char *name;
    int graphic;        /* 1: drawn at 8x8, no supersampling */
    void (*init)(void *state, uint32_t *rng);
    void (*render)(void *state, viz_scene_io_t *io);
} viz_scene_t;

extern const viz_scene_t viz_scenes[SCENE_COUNT];

/* small shared helpers */
float viz_noise3(float x, float y, float z);       /* gradient noise, about -1..1 */
float viz_fbm3(float x, float y, float z, int octaves);
uint32_t viz_rand(uint32_t *s);
float viz_randf(uint32_t *s);                       /* 0..1 */

#endif
