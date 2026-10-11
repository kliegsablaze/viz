/*
 * scenes.c — the pictures (DESIGN.md, "Scenes").
 *
 * 0.1.0 carries six organic scenes (Aurora, Nebula, Tide, Fireflies, Reeds,
 * Lava), drawn on a 32x32 field that the renderer averages down to the pads,
 * and three Graphic ones (Bars, Pulse, Scanner), drawn straight at 8x8.
 *
 * A scene writes a brightness and a gradient coordinate per sample; colour is
 * the renderer's job. Every scene reads the same inputs (viz_scene_io_t), so
 * every scene works in every MODE. The shared grammar: the kick is an impulse,
 * mids a secondary ripple, hats glints, the bands pressure, the beat
 * breathing. Nothing jumps: positions are real numbers and inputs are smoothed.
 *
 * Coordinates: u = x across, v = y down, both 0..1; y = 0 is the top row.
 */
#include <math.h>
#include <string.h>

#include "viz.h"

const char *const viz_scene_names[SCENE_COUNT] = {
    "Aurora", "Nebula", "Tide", "Fireflies", "Reeds", "Lava", "Bars", "Pulse", "Scanner"
};

#define TAU 6.2831853f
#define F2 (FIELD * FIELD)

static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static float smooth(float x, float target, float rate, float dt) {
    float k = rate * dt;
    return x + (target - x) * (k > 1 ? 1 : k);
}
static float smoothstep(float a, float b, float x) {
    float t = clampf((x - a) / (b - a), 0, 1);
    return t * t * (3 - 2 * t);
}
/* Band level at a fractional position across the grid, 0..1 (smooth between bands). */
static float band_at(const float *band, float u) {
    float fb = u * NBAND - 0.5f;
    int b0 = (int)floorf(fb);
    float fr = fb - b0;
    int a = b0 < 0 ? 0 : b0 > NBAND - 1 ? NBAND - 1 : b0;
    int b = b0 + 1 < 0 ? 0 : b0 + 1 > NBAND - 1 ? NBAND - 1 : b0 + 1;
    return band[a] + (band[b] - band[a]) * fr;
}
/* Beat breathing: 1 on the beat, easing back. */
static float breath(const viz_scene_io_t *io, float sharp) {
    return expf(-io->beat_phase * sharp);
}

/* ---- Aurora: curtains hanging from a bright lower hem --------------- */

typedef struct { float lift[NBAND]; float shimmer; } aurora_t;

static void aurora_init(void *st, uint32_t *rng) { (void)rng; memset(st, 0, sizeof(aurora_t)); }

static void aurora_render(void *st, viz_scene_io_t *io) {
    aurora_t *s = st;
    const viz_features_t *f = io->f;
    const viz_look_t *L = io->look;
    for (int b = 0; b < NBAND; b++) s->lift[b] = smooth(s->lift[b], f->band[b] * io->react, 5, io->rdt);
    s->shimmer = smooth(s->shimmer, f->hit[2] * io->react, 10, io->rdt);
    int n = 1 + (int)(L->grow * 2.0f + 0.5f);              /* one to three curtains */
    float fold = 0.9f + L->warp * 2.6f;
    float pulse = 0.75f + 0.25f * breath(io, 3.5f);
    float t = io->t;
    for (int x = 0; x < FIELD; x++) {
        float u = (x + 0.5f) / FIELD;
        float lift = band_at(s->lift, u);
        float hem[3], glow[3], tail[3];
        for (int k = 0; k < n; k++) {
            /* the hem wanders across the sky; the music lifts it */
            hem[k] = 0.62f - 0.2f * k + 0.2f * viz_noise3(u * fold + k * 7.1f, t * 0.11f, k * 1.3f) - 0.2f * lift;
            /* the curtain is bright in places and missing in others */
            glow[k] = smoothstep(-0.35f, 0.35f, viz_noise3(u * 2.2f + k * 5.3f, t * 0.17f, 3.0f + k));
            tail[k] = 0.22f + 0.14f * (viz_noise3(u * 1.5f + k, t * 0.2f, 8.0f) + 1);
        }
        for (int y = 0; y < FIELD; y++) {
            float v = (y + 0.5f) / FIELD;
            float I = 0.012f, cw = 0, cs = 1e-4f;
            for (int k = 0; k < n; k++) {
                float d = hem[k] - v;                        /* > 0: above the hem */
                float c;
                if (d < 0) c = expf(-(d * d) / 0.0012f);      /* a sharp lower edge */
                else c = expf(-d / tail[k]) * (0.55f + 0.45f * viz_noise3(u * 9.0f + k * 2.0f, t * 0.6f, v * 2.0f));
                c *= 2.4f * glow[k] * (1.0f - 0.2f * k) * pulse;
                if (d > -0.03f && d < 0.05f && s->shimmer > 0.01f)
                    c += s->shimmer * 0.8f * fmaxf(0, viz_noise3(u * 16.0f, t * 7.0f, k * 3.0f));
                I += c;
                cw += c * (0.08f * k + 0.45f * clampf(d / 0.5f, 0, 1));
                cs += c;
            }
            io->inten[y * FIELD + x] = I;
            io->coord[y * FIELD + x] = cw / cs + 0.02f * t;
        }
    }
}

/* ---- Nebula: domain-warped fractal clouds ------------------------------- */

typedef struct { float push, open; } nebula_t;

static void nebula_init(void *st, uint32_t *rng) { (void)rng; memset(st, 0, sizeof(nebula_t)); }

static void nebula_render(void *st, viz_scene_io_t *io) {
    nebula_t *s = st;
    const viz_features_t *f = io->f;
    const viz_look_t *L = io->look;
    s->push += io->new_hits[0] * io->react * 0.7f;
    s->push *= expf(-io->rdt * 2.5f);
    s->open = smooth(s->open, f->bright * io->react, 2, io->rdt);
    float scale = 1.3f + L->grow * 2.2f;
    int oct = 2 + (int)(L->grow * 2.0f + 0.5f);
    float warpd = 0.5f + L->warp * 2.0f + s->push;
    float t = io->t * 0.07f;
    float ca = cosf(io->t * 0.02f), sa = sinf(io->t * 0.02f);
    float br = 0.8f + 0.2f * breath(io, 3.0f);
    for (int y = 0; y < FIELD; y++)
        for (int x = 0; x < FIELD; x++) {
            float u = (x + 0.5f) / FIELD - 0.5f, v = (y + 0.5f) / FIELD - 0.5f;
            float px = (u * ca - v * sa) * scale, py = (u * sa + v * ca) * scale;
            float qx = viz_fbm3(px, py, t, oct);
            float qy = viz_fbm3(px + 5.2f, py + 1.3f, t + 3.1f, oct);
            float val = viz_fbm3(px + warpd * qx, py + warpd * qy, t * 1.3f + 7.7f, oct);
            /* dark lanes between bright clouds: most of the sky is near black */
            float I = smoothstep(-0.3f, 0.35f, val + 0.08f * s->open);
            I = I * I * (1.3f + 0.4f * s->open + 0.5f * s->push) * br + 0.015f;
            io->inten[y * FIELD + x] = I;
            io->coord[y * FIELD + x] = 1.2f * val + 0.7f * sqrtf(qx * qx + qy * qy) + 0.15f * s->open;
        }
}

/* ---- Tide: a wave-equation pool; stones fall on the beat ---------------- */

typedef struct { float u[F2], p[F2]; float quiet; } tide_t;

static void tide_init(void *st, uint32_t *rng) { (void)rng; memset(st, 0, sizeof(tide_t)); }

static void tide_drop(tide_t *s, float cx, float cy, float amp, float rad) {
    int r = (int)ceilf(rad * 2);
    for (int y = (int)cy - r; y <= (int)cy + r; y++)
        for (int x = (int)cx - r; x <= (int)cx + r; x++) {
            if (x < 1 || y < 1 || x >= FIELD - 1 || y >= FIELD - 1) continue;
            float dx = x - cx, dy = y - cy;
            s->u[y * FIELD + x] += amp * expf(-(dx * dx + dy * dy) / (rad * rad));
        }
}

static void tide_render(void *st, viz_scene_io_t *io) {
    tide_t *s = st;
    const viz_features_t *f = io->f;
    const viz_look_t *L = io->look;
    /* Where a stone lands follows the loudest band; the row is chance. */
    int loud = 0;
    for (int b = 1; b < NBAND; b++) if (f->band[b] > f->band[loud]) loud = b;
    float bx = 2 + (loud + 0.5f) / NBAND * (FIELD - 4);
    if (io->beat_tick) tide_drop(s, bx + (viz_randf(io->rng) - 0.5f) * 8, 3 + viz_randf(io->rng) * (FIELD - 6), 0.7f, 2.2f);
    if (io->new_hits[0]) tide_drop(s, bx, 4 + viz_randf(io->rng) * (FIELD - 8), 1.6f * io->react, 2.8f);
    if (io->new_hits[1]) tide_drop(s, 3 + viz_randf(io->rng) * (FIELD - 6), 3 + viz_randf(io->rng) * (FIELD - 6), 0.6f * io->react, 1.4f);
    s->quiet += io->rdt;
    if (s->quiet > 1.4f) {               /* never a dead pool: a stray drop now and then */
        tide_drop(s, 3 + viz_randf(io->rng) * (FIELD - 6), 3 + viz_randf(io->rng) * (FIELD - 6), 0.35f, 1.6f);
        s->quiet = 0;
    }
    if (io->beat_tick || io->new_hits[0]) s->quiet = 0;

    int steps = (int)clampf(io->dt * 45.0f + 0.5f, 1, 4);
    float c2 = 0.22f;
    float damp = 0.975f + 0.02f * L->bloom;
    for (int n = 0; n < steps; n++) {
        /* the next height goes where the previous one was, then the two swap */
        for (int y = 1; y < FIELD - 1; y++)
            for (int x = 1; x < FIELD - 1; x++) {
                int i = y * FIELD + x;
                float lap = s->u[i - 1] + s->u[i + 1] + s->u[i - FIELD] + s->u[i + FIELD] - 4 * s->u[i];
                s->p[i] = (2 * s->u[i] - s->p[i] + c2 * lap) * damp;
            }
        for (int i = 0; i < F2; i++) { float t = s->u[i]; s->u[i] = s->p[i]; s->p[i] = t; }
    }
    float gain = 0.6f + 0.8f * L->warp;
    for (int y = 0; y < FIELD; y++)
        for (int x = 0; x < FIELD; x++) {
            int i = y * FIELD + x;
            float h = s->u[i];
            float gx = x > 0 && x < FIELD - 1 ? s->u[i + 1] - s->u[i - 1] : 0;
            float gy = y > 0 && y < FIELD - 1 ? s->u[i + FIELD] - s->u[i - FIELD] : 0;
            float caustic = sqrtf(gx * gx + gy * gy) * gain;
            float v = (y + 0.5f) / FIELD;
            io->inten[i] = 0.035f + 0.03f * (1 - v) + 0.5f * clampf(h, -0.3f, 1.2f) + 1.0f * caustic;
            io->coord[i] = 0.45f + 0.3f * h + 0.25f * v + 0.02f * io->t;
        }
}

/* ---- Fireflies: Kuramoto oscillators that fall into step with the clock -- */

#define FF_MAX 36
typedef struct {
    float x[FF_MAX], y[FF_MAX], th[FF_MAX], spread[FF_MAX], glow[FF_MAX], seed[FF_MAX];
    float kc;           /* coupling to Move's clock: rises while playing */
} fireflies_t;

static void fireflies_init(void *st, uint32_t *rng) {
    fireflies_t *s = st;
    memset(s, 0, sizeof *s);
    for (int i = 0; i < FF_MAX; i++) {
        s->x[i] = 0.08f + 0.84f * viz_randf(rng);
        s->y[i] = 0.08f + 0.84f * viz_randf(rng);
        s->th[i] = TAU * viz_randf(rng);
        s->spread[i] = viz_randf(rng) - 0.5f;
        s->seed[i] = 100 * viz_randf(rng);
    }
}

static void fireflies_render(void *st, viz_scene_io_t *io) {
    fireflies_t *s = st;
    const viz_features_t *f = io->f;
    const viz_look_t *L = io->look;
    int n = 4 + (int)(L->grow * 12 + 0.5f);
    float hz = f->bpm > 20 ? f->bpm / 60.0f : 2.0f;
    float spread = 0.08f + 0.25f * L->warp;
    /* order arrives over about sixteen beats of clock, and leaves when it stops */
    float target = f->clock ? 3.0f : 0.0f;
    s->kc = smooth(s->kc, target, f->clock ? 0.12f : 0.25f, io->rdt);
    float km = 0.35f;
    double beat = f->beat;
    float clock_th = TAU * (float)(beat - floor(beat));
    float dth[FF_MAX];
    for (int i = 0; i < n; i++) {
        float sum = 0;
        for (int j = 0; j < n; j++) if (j != i) sum += sinf(s->th[j] - s->th[i]);
        dth[i] = TAU * hz * (1 + spread * s->spread[i]) + km * sum / n + s->kc * sinf(clock_th - s->th[i]);
    }
    int hits = io->new_hits[0] + io->new_hits[1] + io->new_hits[2];
    for (int i = 0; i < n; i++) {
        s->th[i] = fmodf(s->th[i] + dth[i] * io->rdt, TAU);
        if (hits && viz_randf(io->rng) < 0.35f) {
            s->glow[i] += 0.7f * io->react;
            s->th[i] -= 0.15f * sinf(s->th[i]);   /* a hit tightens the swarm a little */
        }
        s->glow[i] *= expf(-io->rdt * 4.0f);
        float a = s->seed[i];
        s->x[i] += 0.05f * viz_noise3(a, io->t * 0.15f, 0.5f) * io->dt;
        s->y[i] += 0.05f * viz_noise3(a, io->t * 0.15f, 9.5f) * io->dt;
        if (s->x[i] < 0.05f || s->x[i] > 0.95f) s->x[i] = clampf(s->x[i], 0.05f, 0.95f);
        if (s->y[i] < 0.05f || s->y[i] > 0.95f) s->y[i] = clampf(s->y[i], 0.05f, 0.95f);
    }
    for (int k = 0; k < F2; k++) { io->inten[k] = 0.008f; io->coord[k] = 0.1f; }
    float sig = 1.6f + 1.0f * L->bloom;
    int r = (int)ceilf(sig * 2.5f);
    for (int i = 0; i < n; i++) {
        float c = cosf(s->th[i]);
        float flash = c > 0 ? powf(c, 12.0f) : 0;
        float b = 0.05f + 2.4f * flash + 1.5f * s->glow[i];
        float cx = s->x[i] * FIELD - 0.5f, cy = s->y[i] * FIELD - 0.5f;
        for (int y = (int)cy - r; y <= (int)cy + r; y++)
            for (int x = (int)cx - r; x <= (int)cx + r; x++) {
                if (x < 0 || y < 0 || x >= FIELD || y >= FIELD) continue;
                float dx = x - cx, dy = y - cy;
                float g = b * expf(-(dx * dx + dy * dy) / (2 * sig * sig));
                int k = y * FIELD + x;
                float before = io->inten[k];
                io->inten[k] += g;
                io->coord[k] = (io->coord[k] * before + (0.15f + 0.45f * i / n + 0.3f * flash) * g) / (before + g);
            }
    }
}

/* ---- Reeds: eight stalks whose heights are the bands -------------------- */

typedef struct { float h[NBAND], gust, head; } reeds_t;

static void reeds_init(void *st, uint32_t *rng) {
    (void)rng;
    reeds_t *s = st;
    memset(s, 0, sizeof *s);
    for (int k = 0; k < NBAND; k++) s->h[k] = 0.3f;
}

static void reeds_render(void *st, viz_scene_io_t *io) {
    reeds_t *s = st;
    const viz_features_t *f = io->f;
    const viz_look_t *L = io->look;
    float rest = 0.25f + 0.08f * sinf(io->t * 0.7f);
    for (int k = 0; k < NBAND; k++) {
        float target = f->sound ? 0.12f + 0.85f * clampf(f->band[k] * (0.4f + io->react), 0, 1)
                                : rest + 0.05f * sinf(io->t + k);
        s->h[k] = smooth(s->h[k], target, target > s->h[k] ? 12.0f : 3.0f, io->rdt);
    }
    if (io->beat_tick) s->gust = 0.35f + 0.65f * (f->sound ? io->react : 0.5f);
    s->gust *= expf(-io->rdt * 2.0f);
    s->head = smooth(s->head, f->hit[2] * io->react, 12, io->rdt);
    float wind = 0.05f + 0.08f * L->warp;
    float width = 0.045f + 0.03f * L->grow;
    float sway[NBAND], tipx[NBAND];
    for (int k = 0; k < NBAND; k++) {
        sway[k] = wind * viz_noise3(k * 1.7f, io->t * 0.5f, 0.3f) + s->gust * 0.07f * sinf(io->t * 3.0f + k * 0.6f);
        tipx[k] = (k + 0.5f) / NBAND + sway[k];
    }
    for (int y = 0; y < FIELD; y++) {
        float v = (y + 0.5f) / FIELD;
        float up = 1 - v;                         /* 0 at the ground, 1 at the top */
        for (int x = 0; x < FIELD; x++) {
            float u = (x + 0.5f) / FIELD;
            float I = 0.01f + 0.04f * (1 - up) * (1 - up) * (1 - up), cw = 0.05f * I, cs = I;
            for (int k = 0; k < NBAND; k++) {
                float h = s->h[k];
                float c = 0;
                if (up <= h) {
                    float a = up / (h + 1e-3f);
                    float xc = (k + 0.5f) / NBAND + sway[k] * a * a;
                    float d = (u - xc) / (width * (1.1f - 0.5f * a));
                    c = expf(-d * d) * (0.2f + 1.1f * a * a);
                }
                float dx = u - tipx[k], dy = up - h;
                float head = expf(-(dx * dx + dy * dy) / (0.0036f + 0.004f * L->bloom)) * (1.0f + 0.8f * s->head);
                I += c + head;
                cw += c * (0.1f + 0.5f * up) + head * 0.8f;
                cs += c + head;
            }
            io->inten[y * FIELD + x] = I;
            io->coord[y * FIELD + x] = cw / cs + 0.02f * io->t;
        }
    }
}

/* ---- Lava: metaballs drifting like a lava lamp -------------------------- */

#define LAVA_MAX 8
#define LAVA_DROPS 3
typedef struct {
    float ph[LAVA_MAX], w[LAVA_MAX], x0[LAVA_MAX], r0[LAVA_MAX];
    float dx[LAVA_DROPS], dy[LAVA_DROPS], life[LAVA_DROPS];
    float swell;
} lava_t;

static void lava_init(void *st, uint32_t *rng) {
    lava_t *s = st;
    memset(s, 0, sizeof *s);
    for (int i = 0; i < LAVA_MAX; i++) {
        s->ph[i] = TAU * viz_randf(rng);
        s->w[i] = 0.18f + 0.22f * viz_randf(rng);
        s->x0[i] = 0.15f + 0.7f * viz_randf(rng);
        s->r0[i] = 0.07f + 0.05f * viz_randf(rng);
    }
}

static void lava_render(void *st, viz_scene_io_t *io) {
    lava_t *s = st;
    const viz_features_t *f = io->f;
    const viz_look_t *L = io->look;
    int n = 4 + (int)(L->grow * (LAVA_MAX - 4) + 0.5f);
    float bass = 0.5f * (f->band[0] + f->band[1]);
    float target = 1 + 0.45f * bass * io->react + 0.3f * f->hit[0] * io->react + 0.08f * breath(io, 4.0f);
    s->swell = smooth(s->swell, target, 8, io->rdt);
    if (io->new_hits[1]) {                       /* mids split a drop off */
        for (int d = 0; d < LAVA_DROPS; d++)
            if (s->life[d] <= 0) {
                int i = (int)(viz_randf(io->rng) * n) % n;
                s->dx[d] = s->x0[i];
                s->dy[d] = 0.5f + 0.38f * sinf(io->t * s->w[i] + s->ph[i]);
                s->life[d] = 1;
                break;
            }
    }
    float bx[LAVA_MAX + LAVA_DROPS], by[LAVA_MAX + LAVA_DROPS], br2[LAVA_MAX + LAVA_DROPS];
    int m = 0;
    for (int i = 0; i < n; i++, m++) {
        bx[m] = s->x0[i] + 0.12f * viz_noise3(i * 3.3f, io->t * 0.1f, 1.0f);
        by[m] = 0.5f + 0.38f * sinf(io->t * s->w[i] + s->ph[i]);
        float r = s->r0[i] * s->swell;
        br2[m] = r * r;
    }
    for (int d = 0; d < LAVA_DROPS; d++)
        if (s->life[d] > 0) {
            s->dy[d] -= io->rdt * 0.25f;
            s->life[d] -= io->rdt * 0.5f;
            bx[m] = s->dx[d]; by[m] = s->dy[d];
            float r = 0.06f * s->life[d];
            br2[m++] = r * r;
        }
    float th = 1.0f - 0.35f * L->warp;
    for (int y = 0; y < FIELD; y++)
        for (int x = 0; x < FIELD; x++) {
            float u = (x + 0.5f) / FIELD, v = (y + 0.5f) / FIELD;
            float F = 0;
            for (int i = 0; i < m; i++) {
                float ddx = u - bx[i], ddy = v - by[i];
                F += br2[i] / (ddx * ddx + ddy * ddy + 1e-4f);
            }
            float body = smoothstep(th * 0.85f, th * 1.15f, F);
            float glow = 0.22f * smoothstep(0.45f * th, th * 0.85f, F);
            io->inten[y * FIELD + x] = 0.015f + body * (1.0f + 0.12f * clampf(F - th, 0, 3)) + glow;
            io->coord[y * FIELD + x] = 0.1f + 0.1f * clampf(F, 0, 4) + 0.25f * v + 0.01f * io->t;
        }
}

/* ---- Graphic: Bars, the classic eight-band equalizer -------------------- */

typedef struct { float lvl[8], peak[8], hold[8], flash; } bars_t;

static void bars_init(void *st, uint32_t *rng) { (void)rng; memset(st, 0, sizeof(bars_t)); }

static void bars_render(void *st, viz_scene_io_t *io) {
    bars_t *s = st;
    const viz_features_t *f = io->f;
    const viz_look_t *L = io->look;
    float fall = 1.0f + 3.0f * (1 - L->bloom);          /* grid heights per second */
    if (io->beat_tick) s->flash = 1;
    s->flash *= expf(-io->rdt * 6);
    for (int x = 0; x < 8; x++) {
        float in = f->sound ? clampf(f->band[x] * (0.5f + io->react), 0, 1)
                            : (0.5f + 0.4f * sinf(TAU * ((float)f->beat * 0.25f + x / 8.0f))) * breath(io, 2.0f);
        float h = in * 8;
        s->lvl[x] = h > s->lvl[x] ? h : fmaxf(h, s->lvl[x] - fall * 2 * io->rdt);
        if (s->lvl[x] >= s->peak[x]) { s->peak[x] = s->lvl[x]; s->hold[x] = 0.4f; }
        else if ((s->hold[x] -= io->rdt) < 0) s->peak[x] = fmaxf(s->lvl[x], s->peak[x] - fall * io->rdt);
        for (int row = 0; row < 8; row++) {            /* row 0 is the bottom */
            int y = 7 - row;
            float lit = clampf(s->lvl[x] - row, 0, 1);
            float I = lit, C = 0.08f + 0.09f * row;
            int pk = (int)floorf(s->peak[x] - 0.001f);
            if (pk == row && s->peak[x] > 0.5f) { I = fmaxf(I, 0.9f + 0.4f * s->flash); C = 0.85f; }
            io->inten[y * 8 + x] = I + 0.03f;
            io->coord[y * 8 + x] = C + 0.03f * L->warp * sinf(io->t + x);
        }
    }
}

/* ---- Graphic: Pulse, the whole grid breathes ----------------------------- */

typedef struct { float kick; float beats; } pulse_t;

static void pulse_init(void *st, uint32_t *rng) { (void)rng; memset(st, 0, sizeof(pulse_t)); }

static void pulse_render(void *st, viz_scene_io_t *io) {
    pulse_t *s = st;
    const viz_features_t *f = io->f;
    const viz_look_t *L = io->look;
    s->kick = fmaxf(s->kick * expf(-io->rdt * 7), f->hit[0] * io->react);
    if (io->beat_tick) s->beats += 1;
    float b = breath(io, 2.5f + 4.0f * (1 - L->bloom));
    float floor_ = 0.03f + 0.25f * f->level * io->react;
    float fall = 0.5f + 0.5f * L->warp;
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++) {
            float dx = (x - 3.5f) / 4.95f, dy = (y - 3.5f) / 4.95f;
            float r2 = dx * dx + dy * dy;
            float I = floor_ + (0.65f * b + 0.6f * s->kick) * (1 - fall * r2);
            io->inten[y * 8 + x] = I;
            io->coord[y * 8 + x] = 0.13f * s->beats + 0.25f * sqrtf(r2) + 0.1f * L->grow * b;
        }
}

/* ---- Graphic: Scanner, a column sweeping across, one per 16th ------------ */

typedef struct { float trail[64]; } scanner_t;

static void scanner_init(void *st, uint32_t *rng) { (void)rng; memset(st, 0, sizeof(scanner_t)); }

static void scanner_render(void *st, viz_scene_io_t *io) {
    scanner_t *s = st;
    const viz_features_t *f = io->f;
    const viz_look_t *L = io->look;
    float decay = expf(-io->rdt / (0.06f + 0.5f * L->bloom));
    for (int i = 0; i < 64; i++) s->trail[i] *= decay;
    long step = (long)floor(f->beat * 4);
    int col = (int)(((step % 16) + 16) % 16);
    col = col < 8 ? col : 15 - col;                  /* across and back over a bar of 16ths */
    float h = f->sound ? 2 + 6 * clampf(f->band[col] * (0.5f + io->react), 0, 1) : 8;
    for (int row = 0; row < 8; row++) {
        float lit = clampf(h - row, 0, 1);
        int i = (7 - row) * 8 + col;
        if (lit > s->trail[i]) s->trail[i] = lit;
    }
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++) {
            io->inten[y * 8 + x] = 0.03f + s->trail[y * 8 + x];
            io->coord[y * 8 + x] = 0.2f + 0.07f * x + 0.05f * L->grow * (7 - y);
        }
}

const viz_scene_t viz_scenes[SCENE_COUNT] = {
    { "Aurora",    0, aurora_init,    aurora_render },
    { "Nebula",    0, nebula_init,    nebula_render },
    { "Tide",      0, tide_init,      tide_render },
    { "Fireflies", 0, fireflies_init, fireflies_render },
    { "Reeds",     0, reeds_init,     reeds_render },
    { "Lava",      0, lava_init,      lava_render },
    { "Bars",      1, bars_init,      bars_render },
    { "Pulse",     1, pulse_init,     pulse_render },
    { "Scanner",   1, scanner_init,   scanner_render },
};
