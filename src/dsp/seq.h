/*
 * seq.h — the Sequence and Mod pages (DESIGN.md, "The Sequence page", "The Mod
 * page"): eight steps that take turns at the Look page's knobs, then two LFOs
 * that move the result. Neither ever writes a knob: they work on a copy of the
 * look, each frame, on the render thread.
 *
 * Pure C, no host, so the tests drive it directly.
 */
#ifndef VIZ_SEQ_H
#define VIZ_SEQ_H

#include <stdint.h>

#include "viz.h"

#define SEQ_STEPS 8
#define SEQ_MODS  2

/* Option lists, as served. The step fields share two leading options. */
enum { OPT_RANDOM = 0, OPT_SAME = 1 };
enum { BARS_OFF = 0, BARS_RANDOM = 1 };           /* then 1 beat .. 8 bars */
#define SEQ_BARS_OPTS   8
#define SEQ_COLOR_OPTS  (2 + SHAPE_COLORS)
#define SEQ_RATE_OPTS   (2 + SEQ_RATES)
#define SEQ_SIZE_OPTS   (2 + SHAPE_SIZES)
#define SEQ_FOLD_OPTS   (2 + SHAPE_MIRRORS)
#define SEQ_SCENE_OPTS  (1 + SCENE_COUNT)         /* Random, then every scene */
#define SEQ_RATES 7     /* 4 bars, 2 bars, then the Look page's five */

extern const char *const viz_seq_bars_names[SEQ_BARS_OPTS];
extern const char *const viz_seq_color_names[SEQ_COLOR_OPTS];
extern const char *const viz_seq_rate_names[SEQ_RATE_OPTS];
extern const char *const viz_seq_size_names[SEQ_SIZE_OPTS];
extern const char *const viz_seq_fold_names[SEQ_FOLD_OPTS];

enum { MOD_SINE, MOD_RAMP, MOD_SQUARE, MOD_RANDOM, MOD_KICK, MOD_LEVEL, MOD_KINDS };
#define MOD_RATES 9     /* 1/16 .. 16 bars */
enum { AIM_SCENE, AIM_COLOR, AIM_RATE, AIM_SIZE, AIM_TRAIL, AIM_FOLD, AIM_SHIFT, AIM_REACT,
       AIM_LIGHT, AIM_TURN, MOD_AIMS };
extern const char *const viz_mod_kind_names[MOD_KINDS];
extern const char *const viz_mod_rate_names[MOD_RATES];
extern const char *const viz_mod_aim_names[MOD_AIMS];

typedef struct { int scene, bars, color, rate, size, fold; } viz_seq_step_t;   /* option indexes */
typedef struct { int kind, rate, aim; float depth; } viz_mod_t;                 /* depth -1..1 */

typedef struct {
    int play;
    viz_seq_step_t step[SEQ_STEPS];
    viz_mod_t mod[SEQ_MODS];
} viz_seq_params_t;

typedef struct {
    int    cur;                 /* the step playing, -1 for none */
    int    valid;
    double start, len;          /* the step's start and length, in beats */
    int    draw[5];             /* its Random picks: scene, color, rate, size, fold */
    uint32_t kicks;
    double last_kick;           /* beat of the last kick, for MOD_KICK */
    float  mod_value[SEQ_MODS]; /* the LFOs' last values, -1..1 (0..1 for Kick, Level) */
} viz_seq_t;

void viz_seq_init(viz_seq_t *s);
/* Defaults: PLAY Off; step 1 one bar, the rest Off; Random scene and colour,
 * Same for the rest; both LFOs at depth 0. */
void viz_seq_defaults(viz_seq_params_t *p);
/* Applies the sequence, then the LFOs, to a look built from the knobs. */
void viz_seq_apply(viz_seq_t *s, const viz_seq_params_t *p, const viz_features_t *f, viz_look_t *L);
/* A step's length in beats for a bars option (0 for Off; Random is not one). */
double viz_seq_bars_beats(int opt, int beats_per_bar);

#endif
