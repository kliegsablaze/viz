/*
 * features.c — listening (DESIGN.md, "What Viz listens to").
 *
 * Runs on the audio thread, once per 128-frame block: eight bandpass biquads,
 * an envelope per band, an automatic level, three hit detectors (spectral flux
 * on band groups) and Viz's own beat. A few thousand multiply-adds a block, no
 * FFT, no allocation.
 */
#include <math.h>
#include <string.h>

#include "viz.h"

static const float k_centre[NBAND] = { 60, 120, 250, 500, 1000, 2000, 4000, 8000 };
/* Music falls about 3 dB an octave; this lifts the upper bands to meet the
 * lower ones, so a balanced mix lights all eight about equally. */
static const float k_tilt[NBAND]   = { 1.0f, 1.25f, 1.6f, 2.0f, 2.5f, 3.2f, 4.0f, 5.0f };
static const int   k_group[NBAND]  = { 0, 0, 1, 1, 1, 2, 2, 2 };   /* low, mid, high */
static const float k_hit_decay[3]  = { 0.12f, 0.10f, 0.07f };      /* seconds */
static const float k_refractory[3] = { 0.12f, 0.10f, 0.06f };

#define DT ((float)VIZ_BLOCK / VIZ_SR)
#define SILENT_RMS 0.0007f      /* about -63 dBFS */

void viz_listen_init(viz_listen_t *l) {
    memset(l, 0, sizeof *l);
    for (int b = 0; b < NBAND; b++) {
        /* RBJ bandpass, 0 dB peak, about an octave wide */
        float w0 = 2.0f * (float)M_PI * k_centre[b] / VIZ_SR;
        float alpha = sinf(w0) / (2.0f * 1.41f);
        float a0 = 1.0f + alpha;
        l->b0[b] = alpha / a0;
        l->b1[b] = 0.0f;
        l->b2[b] = -alpha / a0;
        l->a1[b] = -2.0f * cosf(w0) / a0;
        l->a2[b] = (1.0f - alpha) / a0;
    }
    l->ref = 1e-3f;
    for (int g = 0; g < 3; g++) l->grp_slow[g] = -20.0f;
    l->own_period = 0.5f;
    l->last_low_hit = -10.0;
}

/* x moves toward y: over tau_rise when y is above it, tau_fall when below. */
static float follow(float x, float y, float tau_rise, float tau_fall) {
    float tau = y > x ? tau_rise : tau_fall;
    return x + (y - x) * (1.0f - expf(-DT / tau));
}

/* A hit pulls Viz's own beat toward it, and nudges its tempo toward the gap
 * since the last one when that gap is a plausible whole number of beats. */
static void own_beat_hit(viz_listen_t *l) {
    double now = l->seconds;
    double gap = now - l->last_low_hit;
    if (gap > 0.25 && gap < 2.0) {
        double n = floor(gap / l->own_period + 0.5);
        if (n >= 1 && n <= 4) {
            double cand = gap / n;
            if (fabs(cand / l->own_period - 1.0) < 0.25)
                l->own_period += (float)(0.2 * (cand - l->own_period));
        }
    }
    if (l->own_period < 0.33f) l->own_period = 0.33f;
    if (l->own_period > 1.2f) l->own_period = 1.2f;
    double frac = l->own_beat - floor(l->own_beat + 0.5);   /* -0.5..0.5 */
    l->own_beat -= 0.4 * frac;
    l->last_low_hit = now;
}

void viz_listen_block(viz_listen_t *l, const int16_t *io, int frames,
                      double clock_beat, float bpm, int beats_per_bar,
                      int mode, viz_features_t *out) {
    float ms[NBAND] = { 0 };
    float total = 0;
    for (int i = 0; i < frames; i++) {
        float x = (io[2 * i] + io[2 * i + 1]) * (0.5f / 32768.0f);
        total += x * x;
        for (int b = 0; b < NBAND; b++) {
            float y = l->b0[b] * x + l->z1[b];
            l->z1[b] = l->b1[b] * x - l->a1[b] * y + l->z2[b];
            l->z2[b] = l->b2[b] * x - l->a2[b] * y;
            ms[b] += y * y;
        }
    }
    float inv = frames > 0 ? 1.0f / frames : 0.0f;
    float rms = sqrtf(total * inv);
    int silent = rms < SILENT_RMS;
    l->silence = silent ? l->silence + DT : 0.0f;
    l->seconds += (double)frames / VIZ_SR;

    /* Bands and the automatic level: rise over ~2 s, fall over ~20 s. */
    float peak = 0;
    float raw[NBAND];
    for (int b = 0; b < NBAND; b++) {
        raw[b] = sqrtf(ms[b] * inv) * k_tilt[b];
        l->env[b] = follow(l->env[b], raw[b], 0.010f, 0.180f);
        if (l->env[b] > peak) peak = l->env[b];
    }
    if (!silent) l->ref = follow(l->ref, peak, 2.0f, 20.0f);
    if (l->ref < 1e-4f) l->ref = 1e-4f;

    /* Hits: half-wave rectified rise of each group's log energy over its own
     * slow average, against an adaptive threshold, with a refractory time. */
    float ge[3] = { 0, 0, 0 };
    for (int b = 0; b < NBAND; b++) ge[k_group[b]] += raw[b] * raw[b];
    float flux[3], rise[3];
    float floor_e = 1e-4f * l->ref * l->ref;   /* energy 40 dB under the level doesn't count */
    for (int g = 0; g < 3; g++) {
        float e = logf(floor_e + ge[g] + 1e-12f);
        flux[g] = e - l->grp_slow[g];
        if (flux[g] < 0) flux[g] = 0;
        l->grp_slow[g] = follow(l->grp_slow[g], e, 0.060f, 0.060f);
        rise[g] = ge[g] - l->grp_lin[g];
        if (rise[g] < 0) rise[g] = 0;
        l->grp_lin[g] = follow(l->grp_lin[g], ge[g], 0.060f, 0.060f);
    }
    for (int g = 0; g < 3; g++) {
        l->flux_mean[g] = follow(l->flux_mean[g], flux[g], 1.0f, 1.0f);
        if (l->hold[g] > 0) l->hold[g] -= DT;
        float thresh = fmaxf(0.9f, 3.0f * l->flux_mean[g]);
        int onset = !silent && l->hold[g] <= 0 && flux[g] > thresh && ge[g] > floor_e;
        if (g == 0) {
            if (onset) {
                l->hit[0] = 1.0f;
                l->hits[0]++;
                l->hold[0] = k_refractory[0];
                own_beat_hit(l);
            }
        } else {
            /* A kick's click reaches the high bands a block before the low
             * filters ring up, so a mid or high onset waits one block: it is
             * that group's own hit only if its rise beats the lows' rise over
             * both blocks (a hat or snare), not a kick's attack. */
            float k = g == 2 ? 0.25f : 0.4f;
            if (l->cand[g] > 0) {
                if (l->cand[g] > k * rise[0]) {
                    l->hit[g] = 1.0f;
                    l->hits[g]++;
                    l->hold[g] = k_refractory[g];
                }
                l->cand[g] = 0;
            } else if (onset && rise[g] > k * rise[0]) {
                l->cand[g] = rise[g];
            }
        }
        l->hit[g] *= expf(-DT / k_hit_decay[g]);
    }

    /* Time. MODE picks where it comes from (DESIGN.md, "Mode"). */
    int playing = clock_beat >= 0;
    int use_clock = playing && mode != MODE_SOUND;
    int sound = mode != MODE_BEAT;
    if (use_clock) {
        l->own_beat = clock_beat;            /* so a stop continues smoothly */
        if (bpm > 20) l->own_period = 60.0f / bpm;
        if (l->own_period < 0.33f) l->own_period = 0.33f;
        if (l->own_period > 1.2f) l->own_period = 1.2f;
    } else if (!sound) {
        l->own_beat += DT / 1.0;             /* Beat mode, stopped: a slow idle */
    } else {
        l->own_beat += DT / l->own_period;
    }
    l->was_clock = use_clock;

    memset(out, 0, sizeof *out);
    if (sound) {
        float sum = 0;
        for (int b = 0; b < NBAND; b++) {
            float v = l->env[b] / l->ref;
            out->band[b] = v > 1 ? 1 : v;
            sum += out->band[b];
        }
        float hi = out->band[5] + out->band[6] + out->band[7];
        out->bright = sum > 1e-4f ? hi / sum : 0;
        out->level = fminf(1.0f, sum / NBAND * 1.6f);
        for (int g = 0; g < 3; g++) { out->hit[g] = l->hit[g]; out->hits[g] = l->hits[g]; }
    }
    out->beat = use_clock ? clock_beat : l->own_beat;
    out->bpm = use_clock && bpm > 20 ? bpm : 60.0f / l->own_period;
    out->beats_per_bar = beats_per_bar > 0 && beats_per_bar <= 16 ? beats_per_bar : 4;
    out->clock = use_clock;
    out->sound = sound;
    out->react_scale = mode == MODE_BLEND && playing ? 2.0f : 1.0f;
    out->silence = l->silence;
    out->seconds = l->seconds;
}
