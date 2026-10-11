/*
 * render.c — from a scene to a finished frame (DESIGN.md, "Rendering",
 * "Colour", "The power guard").
 *
 *   a shape (shapes.c): 64 pads already coloured, folded, kicked and
 *   trailed by the Look page
 *   or an organic scene: a brightness and a gradient coordinate per sample
 *     -> gradient (cosine palette, drifting) -> colour per sample
 *     -> 4x4 box average per pad (the antialiasing)
 *     -> scene change crossfade -> FOLD -> BLOOM (soften, linger)
 *   then for both: TURN -> EDGE (underglow / Launchpad side buttons)
 *   -> LIGHT -> power guard
 *
 * Runs on the render thread on the Move, and in the preview tool on a Mac.
 * Everything is allocated in viz_render_new.
 */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "viz.h"

const char *const viz_color_names[COLOR_COUNT] = {
    "Dusk", "Lagoon", "Ember", "Moss", "Orchid", "Aurora", "Ink", "Prism", "Bone", "Neon"
};

/* Cosine palettes, colour(t) = a + b * cos(2pi (c t + d)) (Inigo Quilez). */
typedef struct { float a[3], b[3], c[3], d[3]; } cospal_t;
static const cospal_t k_pal[COLOR_COUNT] = {
    /* Dusk: violet into amber */
    { {0.55f, 0.35f, 0.50f}, {0.45f, 0.30f, 0.40f}, {1.0f, 1.0f, 1.0f}, {0.00f, 0.12f, 0.30f} },
    /* Lagoon: teal, sea green, deep blue */
    { {0.15f, 0.50f, 0.60f}, {0.15f, 0.35f, 0.40f}, {1.0f, 1.0f, 1.0f}, {0.50f, 0.30f, 0.20f} },
    /* Ember: deep red to gold */
    { {0.65f, 0.28f, 0.08f}, {0.35f, 0.28f, 0.08f}, {1.0f, 1.0f, 0.5f}, {0.00f, 0.10f, 0.20f} },
    /* Moss: greens with a yellow edge */
    { {0.35f, 0.55f, 0.20f}, {0.25f, 0.35f, 0.15f}, {1.0f, 1.0f, 1.0f}, {0.10f, 0.00f, 0.35f} },
    /* Orchid: magenta, lilac, rose */
    { {0.60f, 0.30f, 0.65f}, {0.35f, 0.25f, 0.35f}, {1.0f, 1.0f, 1.0f}, {0.00f, 0.30f, 0.60f} },
    /* Aurora: green into violet */
    { {0.30f, 0.55f, 0.50f}, {0.30f, 0.45f, 0.40f}, {1.0f, 1.0f, 1.0f}, {0.60f, 0.10f, 0.35f} },
    /* Ink: indigo and cold blue */
    { {0.22f, 0.28f, 0.60f}, {0.20f, 0.25f, 0.40f}, {1.0f, 1.0f, 1.0f}, {0.60f, 0.55f, 0.40f} },
    /* Prism: the whole rainbow */
    { {0.50f, 0.50f, 0.50f}, {0.50f, 0.50f, 0.50f}, {1.0f, 1.0f, 1.0f}, {0.00f, 0.33f, 0.67f} },
    /* Bone: warm whites and sand */
    { {0.75f, 0.65f, 0.55f}, {0.25f, 0.22f, 0.20f}, {1.0f, 1.0f, 1.0f}, {0.00f, 0.05f, 0.10f} },
    /* Neon: hot pink, cyan, lime */
    { {0.50f, 0.50f, 0.50f}, {0.50f, 0.50f, 0.50f}, {2.0f, 1.0f, 0.0f}, {0.50f, 0.20f, 0.25f} },
};

/* ---- shared helpers ---------------------------------------------------- */

uint32_t viz_rand(uint32_t *s) {
    uint32_t x = *s ? *s : 0x9E3779B9u;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return *s = x;
}

float viz_randf(uint32_t *s) { return (viz_rand(s) >> 8) * (1.0f / 16777216.0f); }

/* Ken Perlin's improved noise (2002), fixed permutation. */
static const uint8_t k_perm[256] = {
    151,160,137,91,90,15,131,13,201,95,96,53,194,233,7,225,140,36,103,30,69,142,8,99,37,240,
    21,10,23,190,6,148,247,120,234,75,0,26,197,62,94,252,219,203,117,35,11,32,57,177,33,88,
    237,149,56,87,174,20,125,136,171,168,68,175,74,165,71,134,139,48,27,166,77,146,158,231,
    83,111,229,122,60,211,133,230,220,105,92,41,55,46,245,40,244,102,143,54,65,25,63,161,1,
    216,80,73,209,76,132,187,208,89,18,169,200,196,135,130,116,188,159,86,164,100,109,198,
    173,186,3,64,52,217,226,250,124,123,5,202,38,147,118,126,255,82,85,212,207,206,59,227,
    47,16,58,17,182,189,28,42,223,183,170,213,119,248,152,2,44,154,163,70,221,153,101,155,
    167,43,172,9,129,22,39,253,19,98,108,110,79,113,224,232,178,185,112,104,218,246,97,228,
    251,34,242,193,238,210,144,12,191,179,162,241,81,51,145,235,249,14,239,107,49,192,214,
    31,181,199,106,157,184,84,204,176,115,121,50,45,127,4,150,254,138,236,205,93,222,114,67,
    29,24,72,243,141,128,195,78,66,215,61,156,180
};
#define P(i) k_perm[(i) & 255]

static float fade5(float t) { return t * t * t * (t * (t * 6 - 15) + 10); }
static float lerpf(float a, float b, float t) { return a + (b - a) * t; }
static float grad3(int h, float x, float y, float z) {
    h &= 15;
    float u = h < 8 ? x : y, v = h < 4 ? y : (h == 12 || h == 14) ? x : z;
    return ((h & 1) ? -u : u) + ((h & 2) ? -v : v);
}

float viz_noise3(float x, float y, float z) {
    float fx = floorf(x), fy = floorf(y), fz = floorf(z);
    int X = (int)fx, Y = (int)fy, Z = (int)fz;
    x -= fx; y -= fy; z -= fz;
    float u = fade5(x), v = fade5(y), w = fade5(z);
    int A = P(X) + Y, AA = P(A) + Z, AB = P(A + 1) + Z;
    int B = P(X + 1) + Y, BA = P(B) + Z, BB = P(B + 1) + Z;
    return lerpf(lerpf(lerpf(grad3(P(AA), x, y, z), grad3(P(BA), x - 1, y, z), u),
                       lerpf(grad3(P(AB), x, y - 1, z), grad3(P(BB), x - 1, y - 1, z), u), v),
                 lerpf(lerpf(grad3(P(AA + 1), x, y, z - 1), grad3(P(BA + 1), x - 1, y, z - 1), u),
                       lerpf(grad3(P(AB + 1), x, y - 1, z - 1), grad3(P(BB + 1), x - 1, y - 1, z - 1), u), v),
                 w);
}

float viz_fbm3(float x, float y, float z, int octaves) {
    float sum = 0, amp = 0.5f, norm = 0;
    for (int o = 0; o < octaves; o++) {
        sum += amp * viz_noise3(x, y, z);
        norm += amp;
        x = x * 2.03f + 17.1f; y = y * 2.03f + 3.7f; z = z * 1.7f;
        amp *= 0.5f;
    }
    return sum / norm;
}

/* ---- the scene list ------------------------------------------------------ */

const char *viz_scene_names[SCENE_COUNT];
const char *viz_scene_menu[SCENE_COUNT];
int viz_scene_order[SCENE_COUNT];
int viz_scene_pos[SCENE_COUNT];

/* A before B, ignoring case (src/canvas.js sorts the same way). */
static int name_before(const char *a, const char *b) {
    for (;; a++, b++) {
        int x = *a >= 'A' && *a <= 'Z' ? *a + 32 : *a, y = *b >= 'A' && *b <= 'Z' ? *b + 32 : *b;
        if (x != y || !x) return x < y;
    }
}

__attribute__((constructor)) static void scene_names(void) {
    for (int i = 0; i < SHAPE_COUNT; i++) viz_scene_names[i] = viz_shape_names[i];
    for (int i = 0; i < ORGANIC_COUNT; i++) viz_scene_names[SCENE_ORGANIC + i] = viz_organic[i].name;
    /* the menu: every scene in alphabetical order (the user, 2026-10-11) */
    for (int i = 0; i < SCENE_COUNT; i++) {
        int j = i;
        while (j > 0 && name_before(viz_scene_names[i], viz_scene_names[viz_scene_order[j - 1]])) {
            viz_scene_order[j] = viz_scene_order[j - 1];
            j--;
        }
        viz_scene_order[j] = i;
    }
    for (int k = 0; k < SCENE_COUNT; k++) {
        viz_scene_menu[k] = viz_scene_names[viz_scene_order[k]];
        viz_scene_pos[viz_scene_order[k]] = k;
    }
}

/* COLOR's nine sets, as the organic scenes' nearest gradients. */
static const int k_org_color[SHAPE_COLORS] = {
    6 /* Ice: Ink */, 2 /* Fire: Ember */, 9 /* Acid: Neon */, 0 /* Sunset: Dusk */, 3 /* Mint: Moss */,
    4 /* Royal: Orchid */, 4 /* Candy: Orchid */, 8 /* Mono: Bone */, 7 /* Rainbow: Prism */
};
static const float k_org_react[SHAPE_REACTS] = { 0.0f, 0.45f, 0.6f, 0.8f, 1.0f };

/* ---- the renderer ------------------------------------------------------- */

struct viz_render {
    /* organic */
    _Alignas(16) uint8_t state[2][SCENE_STATE_BYTES];
    int   slot_scene[2];    /* organic index, -1 for none */
    int   cur;              /* index of the current slot */
    float fade;             /* < 0: no change under way; else 0..1 toward the other slot */
    float fade_time;
    float t;                /* organic scene time */
    float pal_phase;
    float inten[2][FIELD * FIELD], coord[2][FIELD * FIELD];
    float field_rgb[FIELD * FIELD * 3];
    float pads[2][NPAD][3];
    /* shapes */
    viz_shapes_t shapes;
    double time;
    long  last16;
    uint8_t log[64];
    /* both */
    uint32_t rng;
    uint32_t last_hits[3];
    double last_beat;
    int   started;
    int   was_shape;
    float out[NLIGHT][3];   /* the last frame, before LIGHT: organic BLOOM lingers on it */
};

viz_render_t *viz_render_new(uint32_t seed) {
    viz_render_t *r = calloc(1, sizeof *r);
    if (!r) return NULL;
    r->rng = seed ? seed : 1;
    r->slot_scene[0] = r->slot_scene[1] = -1;
    r->fade = -1;
    r->last16 = -1;
    r->was_shape = -1;
    viz_shapes_init(&r->shapes);
    return r;
}

void viz_render_free(viz_render_t *r) { free(r); }

const float *viz_render_field_rgb(const viz_render_t *r) { return r->field_rgb; }

static void palette(const viz_organic_look_t *look, float phase, float t, float rgb[3]) {
    const cospal_t *p = &k_pal[look->color >= 0 && look->color < COLOR_COUNT ? look->color : 0];
    float m = 0;
    for (int i = 0; i < 3; i++) {
        float v = p->a[i] + p->b[i] * cosf(6.2831853f * (p->c[i] * (t + phase) + p->d[i]));
        v = v < 0 ? 0 : v > 1 ? 1 : v;
        rgb[i] = v;
        if (v > m) m = v;
    }
    /* LEDs show light, not paint: lift each colour toward full strength and
     * leave brightness to the scene, keeping a little of the palette's own
     * shading so dark entries stay deeper. */
    float k = 1.0f / (0.4f + 0.6f * m);
    for (int i = 0; i < 3; i++) rgb[i] *= k;
}

/* Draw one organic scene into its slot and reduce it to 64 pad colours. */
static void draw_slot(viz_render_t *r, int slot, const viz_organic_look_t *look, viz_scene_io_t *io, int keep_field) {
    const viz_scene_t *sc = &viz_organic[r->slot_scene[slot]];
    io->inten = r->inten[slot];
    io->coord = r->coord[slot];
    memset(r->inten[slot], 0, sizeof r->inten[slot]);
    memset(r->coord[slot], 0, sizeof r->coord[slot]);
    sc->render(r->state[slot], io);

    float acc[NPAD][3];
    memset(acc, 0, sizeof acc);
    for (int y = 0; y < FIELD; y++)
        for (int x = 0; x < FIELD; x++) {
            int i = y * FIELD + x;
            float c[3];
            palette(look, r->pal_phase, r->coord[slot][i], c);
            float v = r->inten[slot][i];
            if (v < 0) v = 0;
            if (v > 1.5f) v = 1.5f;
            int pad = (y / 4) * 8 + x / 4;
            for (int k = 0; k < 3; k++) {
                float cv = c[k] * v;
                acc[pad][k] += cv;
                if (keep_field) r->field_rgb[i * 3 + k] = cv > 1 ? 1 : cv;
            }
        }
    for (int i = 0; i < NPAD; i++)
        for (int k = 0; k < 3; k++) r->pads[slot][i][k] = acc[i][k] * (1.0f / 16);
}

/* FOLD on 64 pads, the same fold shapes.c applies to its scenes. */
static void fold(float pads[NPAD][3], int m) {
    if (!m) return;
    float src[NPAD][3];
    memcpy(src, pads, sizeof src);
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++) {
            int sx = x, sy = y;
            if ((m == 1 || m >= 3) && sx > 3) sx = 7 - sx;
            if ((m == 2 || m >= 3) && sy > 3) sy = 7 - sy;
            if (m == 4 && sx > sy) { int t = sx; sx = sy; sy = t; }
            memcpy(pads[y * 8 + x], src[sy * 8 + sx], sizeof src[0]);
        }
}

/* An organic frame: 64 pads, 0..1, before TURN. Returns BLOOM's linger. */
static float organic_frame(viz_render_t *r, const viz_look_t *look, const viz_features_t *f,
                           viz_scene_io_t *io, float dt, float pads[NPAD][3]) {
    int want = look->scene - SCENE_ORGANIC;
    viz_organic_look_t L;
    const viz_shape_knobs_t *k = &look->k;
    L.color = k_org_color[k->color >= 0 && k->color < SHAPE_COLORS ? k->color : 0];
    L.flow = k->rate / (float)(SHAPE_RATES - 1);
    L.grow = k->size / (float)(SHAPE_SIZES - 1);
    L.bloom = k->trail / (float)(SHAPE_TRAILS - 1);
    L.react = k_org_react[k->react >= 0 && k->react < SHAPE_REACTS ? k->react : 1];
    L.warp = 0.4f;
    L.drift = 0.3f;

    if (r->slot_scene[r->cur] < 0) {          /* first organic frame */
        r->slot_scene[r->cur] = want;
        viz_organic[want].init(r->state[r->cur], &r->rng);
    }
    if (r->fade < 0 && want != r->slot_scene[r->cur]) {
        int o = 1 - r->cur;                   /* a new scene seeps in over one beat */
        r->slot_scene[o] = want;
        viz_organic[want].init(r->state[o], &r->rng);
        r->fade = 0;
        float beat_s = f->bpm > 20 ? 60.0f / f->bpm : 0.5f;
        r->fade_time = beat_s < 0.4f ? 0.4f : beat_s > 1.5f ? 1.5f : beat_s;
    }

    /* Time: RATE from a quarter to four times, tempo-relative. */
    float speed = exp2f((L.flow - 0.5f) * 4.0f);
    float tempo = f->bpm > 20 ? f->bpm / 120.0f : 1.0f;
    if (tempo < 0.5f) tempo = 0.5f;
    if (tempo > 2.0f) tempo = 2.0f;
    float sdt = dt * speed * tempo;
    r->t += sdt;
    io->look = &L;
    io->t = r->t;
    io->dt = sdt;
    io->react = f->sound ? L.react * f->react_scale : 0.0f;

    /* The palette's mood creeps, and steps a little on each bar. */
    r->pal_phase += dt * (0.004f + 0.03f * L.drift) + (io->bar_tick ? 0.02f * L.drift : 0);
    if (r->pal_phase > 1000) r->pal_phase -= 1000;

    draw_slot(r, r->cur, &L, io, 1);
    memcpy(pads, r->pads[r->cur], sizeof r->pads[0]);
    if (r->fade >= 0) {
        int o = 1 - r->cur;
        draw_slot(r, o, &L, io, 0);
        r->fade += dt / r->fade_time;
        float m = r->fade > 1 ? 1 : r->fade;
        m = m * m * (3 - 2 * m);
        for (int i = 0; i < NPAD; i++)
            for (int c = 0; c < 3; c++) pads[i][c] = pads[i][c] * (1 - m) + r->pads[o][i][c] * m;
        if (r->fade >= 1) { r->cur = o; r->fade = -1; }
    }
    fold(pads, k->mirror);

    /* BLOOM (TRAIL): a little blur, and light that lingers. */
    float blur = L.bloom * 0.45f;
    float soft[NPAD][3];
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++)
            for (int c = 0; c < 3; c++) {
                float s = 0; int n = 0;
                if (x > 0) { s += pads[y * 8 + x - 1][c]; n++; }
                if (x < 7) { s += pads[y * 8 + x + 1][c]; n++; }
                if (y > 0) { s += pads[(y - 1) * 8 + x][c]; n++; }
                if (y < 7) { s += pads[(y + 1) * 8 + x][c]; n++; }
                float v = pads[y * 8 + x][c];
                soft[y * 8 + x][c] = v + blur * (s / n - v);
            }
    memcpy(pads, soft, sizeof soft);
    return 0.45f + 0.5f * L.bloom;
}

/* A shape frame: 64 pads, 0..1, before TURN. */
static void shape_frame(viz_render_t *r, const viz_look_t *look, const viz_features_t *f,
                        const int new_hits[3], float dt, float pads[NPAD][3]) {
    viz_shape_feat_t sf;
    memset(&sf, 0, sizeof sf);
    r->time += dt;
    sf.beats = f->beat;
    sf.time = r->time;
    sf.dt = dt;
    sf.beats_per_bar = f->beats_per_bar;
    sf.kick_count = f->hits[0];
    if (f->sound) {
        sf.kick = f->hit[0];
        sf.snare = f->hit[1];
        sf.hat = f->hit[2];
        sf.bass = 0.5 * ((double)f->band[0] + f->band[1]);
        sf.kick_now = new_hits[0] > 0;
        sf.snare_now = new_hits[1] > 0;
        sf.hat_now = new_hits[2] > 0;
        memcpy(sf.bands, f->band, sizeof sf.bands);
    }
    /* The last four bars of hits, a 16th per slot, for Steps and Tally. */
    long n16 = (long)floor(f->beat * 4);
    if (r->last16 < 0 || n16 < r->last16 || n16 - r->last16 >= 64) memset(r->log, 0, sizeof r->log);
    else for (long i = r->last16 + 1; i <= n16; i++) r->log[i % 64] = 0;
    r->last16 = n16;
    r->log[n16 % 64] |= (sf.kick_now ? HIT_KICK : 0) | (sf.snare_now ? HIT_SNARE : 0) | (sf.hat_now ? HIT_HAT : 0);
    memcpy(sf.log, r->log, sizeof sf.log);

    viz_shapes_frame(&r->shapes, look->scene, &look->k, &sf);
    for (int i = 0; i < NPAD; i++)
        for (int c = 0; c < 3; c++) pads[i][c] = r->shapes.disp[i * 3 + c] * (1.0f / 255);
}

void viz_render_frame(viz_render_t *r, const viz_look_t *look, const viz_features_t *f,
                      float dt, viz_frame_t *out) {
    viz_look_t lk = *look;
    if (lk.scene < 0 || lk.scene >= SCENE_COUNT) lk.scene = 0;
    look = &lk;
    int shape = look->scene < SHAPE_COUNT;
    if (shape != r->was_shape) {              /* changing kind starts the new kind fresh */
        r->slot_scene[0] = r->slot_scene[1] = -1;
        r->fade = -1;
        r->shapes.scene = -1;
        memset(r->out, 0, sizeof r->out);
        r->was_shape = shape;
    }

    viz_scene_io_t io;
    memset(&io, 0, sizeof io);
    io.f = f;
    io.rdt = dt;
    io.rng = &r->rng;
    for (int g = 0; g < 3; g++) {
        uint32_t d = f->hits[g] - r->last_hits[g];
        io.new_hits[g] = r->started && d < 100 ? (int)d : 0;
        r->last_hits[g] = f->hits[g];
    }
    double beat = f->beat;
    if (r->started && floor(beat) != floor(r->last_beat) && beat > r->last_beat) {
        io.beat_tick = 1;
        long b = (long)floor(beat);
        io.bar_tick = (b % f->beats_per_bar) == 0;
    }
    r->last_beat = beat;
    r->started = 1;
    io.beat_phase = (float)(beat - floor(beat));
    {
        double bars = beat / f->beats_per_bar;
        io.bar_phase = (float)(bars - floor(bars));
    }

    float pads[NPAD][3];
    float linger = 0;                         /* shapes carry their own TRAIL */
    if (shape) shape_frame(r, look, f, io.new_hits, dt, pads);
    else linger = organic_frame(r, look, f, &io, dt, pads);

    /* TURN: quarter turns clockwise, before the edge is taken from the pads. */
    float turned[NPAD][3];
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++) {
            int sx = x, sy = y;
            for (int q = 0; q < (look->turn & 3); q++) { int tx = sy, ty = 7 - sx; sx = tx; sy = ty; }
            memcpy(turned[y * 8 + x], pads[sy * 8 + sx], sizeof turned[0]);
        }

    float lights[NLIGHT][3];
    memcpy(lights, turned, sizeof turned);

    /* EDGE: each side carries the picture beside it, or the whole ring breathes. */
    for (int s = 0; s < 4; s++)
        for (int i = 0; i < 8; i++) {
            float *e = lights[NPAD + s * 8 + i];
            int ax, ay, bx, by;
            switch (s) {
            case EDGE_RIGHT:  ax = 7; ay = i; bx = 6; by = i; break;
            case EDGE_LEFT:   ax = 0; ay = i; bx = 1; by = i; break;
            case EDGE_BOTTOM: ax = i; ay = 7; bx = i; by = 6; break;
            default:          ax = i; ay = 0; bx = i; by = 1; break;
            }
            if (!(look->edge_sides & (1u << s))) {
                e[0] = e[1] = e[2] = 0;          /* this grid has no lights there */
            } else if (look->edge == EDGEMODE_GLOW) {
                for (int k = 0; k < 3; k++)
                    e[k] = shape ? 0.8f * turned[ay * 8 + ax][k]      /* as the gallery shows it */
                                 : 0.7f * (2 * turned[ay * 8 + ax][k] + turned[by * 8 + bx][k]) / 3;
            } else if (look->edge == EDGEMODE_BREATHE) {
                float c[3];
                if (shape) {
                    double d[3];
                    viz_shape_colour(look->k.color, 0, ax, ay, d);
                    for (int k = 0; k < 3; k++) c[k] = (float)(d[k] / 255);
                } else {
                    viz_organic_look_t L = { k_org_color[look->k.color >= 0 && look->k.color < SHAPE_COLORS ? look->k.color : 0],
                                             0, 0, 0, 0, 0, 0 };
                    palette(&L, r->pal_phase, 0.15f * s + 0.02f * i, c);
                }
                float b = 0.12f + 0.6f * expf(-io.beat_phase * 5.0f) * (0.4f + 0.6f * (f->sound ? f->level : 0.6f));
                for (int k = 0; k < 3; k++) e[k] = c[k] * b;
            } else {
                e[0] = e[1] = e[2] = 0;
            }
        }

    for (int i = 0; i < NLIGHT; i++)
        for (int k = 0; k < 3; k++) {
            float v = lights[i][k];
            float p = r->out[i][k] * linger;
            r->out[i][k] = v > p ? v : p;
        }

    /* LIGHT, then the power guard (DESIGN.md, "The power guard"). */
    float sum = 0;
    float v8[NLIGHT][3];
    for (int i = 0; i < NLIGHT; i++) {
        float w = i < NPAD ? 1.0f : look->edge_weight;
        for (int k = 0; k < 3; k++) {
            float v = r->out[i][k] * look->light;
            v = v < 0 ? 0 : v > 1 ? 1 : v;
            v8[i][k] = v * 255.0f;
            sum += w * v8[i][k];
        }
    }
    float scale = 1.0f;
    if (!look->hub && sum > POWER_BUDGET) scale = POWER_BUDGET / sum;
    for (int i = 0; i < NLIGHT; i++)
        for (int k = 0; k < 3; k++) out->rgb[i][k] = (uint8_t)(v8[i][k] * scale);   /* floor: never over */
    out->seq++;
}
