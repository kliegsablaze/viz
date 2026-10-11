/*
 * shapes.h — the hundred pad-sharp scenes and the look that turns them into
 * pad colours (DESIGN.md, "Scenes", "The Look page").
 *
 * The scenes were chosen in the scene gallery (tools/gallery/gallery.html)
 * and this is a line-for-line port of its JavaScript, so a scene looks on the
 * Move the way it looked when it was approved. tests/test_parity.c runs both
 * side by side and fails on any difference.
 *
 * Pure C, no host: the render thread on the Move, the preview tool and the
 * parity test all link it unchanged. Doubles throughout, and float32 where the
 * gallery stores into a Float32Array, so the two agree to the bit.
 */
#ifndef VIZ_SHAPES_H
#define VIZ_SHAPES_H

#include <stdint.h>

#define SHAPE_COUNT 100

/* What a scene may know about the music. On the Move these come from the
 * listener (render.c fills them in); in the parity test, from the gallery's
 * own drum loop. */
typedef struct {
    double beats;           /* continuous, the clock's or Viz's own */
    double time;            /* seconds */
    double dt;              /* seconds since the last frame */
    int    beats_per_bar;   /* SHIFT Bar; 0 means 4 */
    double kick, snare, hat, bass;   /* decaying pulses, 0..1 */
    int    kick_now, snare_now, hat_now;   /* a hit landed since the last frame */
    uint32_t kick_count;    /* kicks so far: SHIFT Kick counts them */
    float  bands[8];        /* 0..1, low to high */
    uint8_t log[64];        /* per 16th note, 64 deep: bit 0 kick, 1 snare, 2 hat */
} viz_shape_feat_t;

enum { HIT_KICK = 1, HIT_SNARE = 2, HIT_HAT = 4 };

/* The Look page, as indexes into each knob's options. */
typedef struct {
    int color;    /* COLOR: Ice Fire Acid Sunset Mint Royal Candy Mono Rainbow */
    int rate;     /* RATE: 1 bar, 1/2, 1/4, 1/8, 1/16 */
    int size;     /* SIZE: 1..4, stored 0..3 */
    int trail;    /* TRAIL: Off Short Medium Long Smear */
    int mirror;   /* FOLD: Off Sides Top Four Eight */
    int shift;    /* SHIFT: Hold Bar Beat Kick */
    int react;    /* REACT: Off Glow Grow Flash Flip */
} viz_shape_knobs_t;

#define SHAPE_COLORS 9
#define SHAPE_RATES 5
#define SHAPE_SIZES 4
#define SHAPE_TRAILS 5
#define SHAPE_MIRRORS 5
#define SHAPE_SHIFTS 4
#define SHAPE_REACTS 5

extern const char *const viz_shape_names[SHAPE_COUNT];
extern const char *const viz_shape_color_names[SHAPE_COLORS];
extern const char *const viz_shape_rate_names[SHAPE_RATES];
extern const char *const viz_shape_size_names[SHAPE_SIZES];
extern const char *const viz_shape_trail_names[SHAPE_TRAILS];
extern const char *const viz_shape_mirror_names[SHAPE_MIRRORS];
extern const char *const viz_shape_shift_names[SHAPE_SHIFTS];
extern const char *const viz_shape_react_names[SHAPE_REACTS];

#define SHAPE_STATE_BYTES 4096

typedef struct {
    int scene;                       /* -1 before the first frame */
    _Alignas(16) uint8_t state[SHAPE_STATE_BYTES];
    float cur[192], out[192], disp[192];   /* 0..255; disp is after TRAIL */
} viz_shapes_t;

void viz_shapes_init(viz_shapes_t *s);
/* Colour ci (0..2) of COLOR set `color` at pad x, y, unshifted: 0..255. */
void viz_shape_colour(int color, int ci, int x, int y, double out[3]);
/* One frame of scene `scene` (0..SHAPE_COUNT-1). A different scene from the
 * last frame starts fresh. The result is s->disp: 64 pads, RGB 0..255. */
void viz_shapes_frame(viz_shapes_t *s, int scene, const viz_shape_knobs_t *k, const viz_shape_feat_t *f);

#endif
