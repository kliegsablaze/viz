/*
 * seq.c — the Sequence and Mod pages (seq.h, DESIGN.md).
 */
#include <math.h>
#include <string.h>

#include "seq.h"

const char *const viz_seq_bars_names[SEQ_BARS_OPTS] = {
    "Off", "Random", "1 beat", "2 beats", "1 bar", "2 bars", "4 bars", "8 bars"
};
const char *const viz_seq_color_names[SEQ_COLOR_OPTS] = {
    "Random", "Same", "Ice", "Fire", "Acid", "Sunset", "Mint", "Royal", "Candy", "Mono", "Rainbow"
};
const char *const viz_seq_rate_names[SEQ_RATE_OPTS] = {
    "Random", "Same", "4 bars", "2 bars", "1 bar", "1/2", "1/4", "1/8", "1/16"
};
const char *const viz_seq_size_names[SEQ_SIZE_OPTS] = { "Random", "Same", "1", "2", "3", "4" };
const char *const viz_seq_fold_names[SEQ_FOLD_OPTS] = { "Random", "Same", "Off", "Sides", "Top", "Four", "Eight" };

const char *const viz_mod_kind_names[MOD_KINDS] = { "Sine", "Ramp", "Square", "Random", "Kick", "Level" };
const char *const viz_mod_rate_names[MOD_RATES] = {
    "1/16", "1/8", "1/4", "1/2", "1 bar", "2 bars", "4 bars", "8 bars", "16 bars"
};
const char *const viz_mod_aim_names[MOD_AIMS] = {
    "Scene", "Color", "Rate", "Size", "Trail", "Fold", "Shift", "React", "Light", "Turn"
};

/* Steps per beat for the seven rates: 4 bars .. 1/16. The Look page's RATE is
 * the last five (its index + 2). */
static const double k_rate7[SEQ_RATES] = { 1.0 / 16, 1.0 / 8, 0.25, 0.5, 1, 2, 4 };

void viz_seq_init(viz_seq_t *s) {
    memset(s, 0, sizeof *s);
    s->cur = -1;
    s->last_kick = -1e9;
}

void viz_seq_defaults(viz_seq_params_t *p) {
    memset(p, 0, sizeof *p);
    for (int i = 0; i < SEQ_STEPS; i++) {
        viz_seq_step_t *st = &p->step[i];
        st->scene = OPT_RANDOM;
        st->bars = i == 0 ? 4 : BARS_OFF;          /* step 1: one bar */
        st->color = st->rate = st->size = OPT_RANDOM;
        st->fold = 2 + 3;                           /* Four */
    }
    p->mod[0] = (viz_mod_t){ MOD_SINE, 4, AIM_COLOR, 0 };
    p->mod[1] = (viz_mod_t){ MOD_KICK, 2, AIM_SIZE, 0 };
}

double viz_seq_bars_beats(int opt, int bpb) {
    if (bpb <= 0) bpb = 4;
    switch (opt) {
    case 2: return 1;
    case 3: return 2;
    case 4: return bpb;
    case 5: return 2.0 * bpb;
    case 6: return 4.0 * bpb;
    case 7: return 8.0 * bpb;
    default: return 0;
    }
}

/* A repeatable random number for a step starting at a beat: the same song
 * position always draws the same. */
static uint32_t mix(uint32_t h) {
    h ^= h >> 16; h *= 0x7feb352dU; h ^= h >> 15; h *= 0x846ca68bU; h ^= h >> 16;
    return h;
}
static int pick(double start, int step, int field, int n) {
    uint32_t q = (uint32_t)(int64_t)llround(start * 4);
    return (int)(mix(q * 0x9E3779B9U ^ (uint32_t)step * 0x85EBCA6BU ^ (uint32_t)field * 0xC2B2AE35U) % (uint32_t)n);
}

static double step_len(const viz_seq_params_t *p, int i, double start, int bpb) {
    int b = p->step[i].bars;
    if (b == BARS_RANDOM) b = 2 + pick(start, i, 9, SEQ_BARS_OPTS - 2);
    return viz_seq_bars_beats(b, bpb);
}

static int next_on(const viz_seq_params_t *p, int from) {
    for (int k = 1; k <= SEQ_STEPS; k++) {
        int j = (from + k) % SEQ_STEPS;
        if (p->step[j].bars != BARS_OFF) return j;
    }
    return -1;
}

static void enter(viz_seq_t *s, const viz_seq_params_t *p, int i, double start, int bpb) {
    s->cur = i;
    s->start = start;
    s->len = step_len(p, i, start, bpb);
    s->draw[0] = pick(start, i, 0, SCENE_COUNT);
    s->draw[1] = pick(start, i, 1, SHAPE_COLORS);
    s->draw[2] = pick(start, i, 2, SEQ_RATES);
    s->draw[3] = pick(start, i, 3, SHAPE_SIZES);
    s->draw[4] = pick(start, i, 4, SHAPE_MIRRORS);
}

/* Where the sequence is at beat b. It runs from beat 0 of the song (or of
 * Viz's own beat), so a bar always shows the same step; it moves on as time
 * passes, and walks again from 0 when time goes back (Move restarted). */
static void follow(viz_seq_t *s, const viz_seq_params_t *p, double b, int bpb) {
    if (!s->valid || s->cur < 0 || b < s->start - 1e-9) {
        int first = next_on(p, SEQ_STEPS - 1);
        if (first < 0) { s->cur = -1; s->valid = 0; return; }
        enter(s, p, first, 0, bpb);
        s->valid = 1;
    }
    /* A step turned Off, or a length edited, applies at once. */
    if (p->step[s->cur].bars == BARS_OFF) s->len = 0;
    else if (p->step[s->cur].bars != BARS_RANDOM) s->len = step_len(p, s->cur, s->start, bpb);
    for (int guard = 0; b >= s->start + s->len; guard++) {
        int nx = next_on(p, s->cur);
        if (nx < 0) { s->cur = -1; s->valid = 0; return; }
        if (guard > 200000) { enter(s, p, nx, b, bpb); break; }   /* hours in: just start here */
        enter(s, p, nx, s->start + s->len, bpb);
    }
}

static double lfo(const viz_seq_t *s, const viz_mod_t *m, int j, const viz_features_t *f) {
    int bpb = f->beats_per_bar > 0 ? f->beats_per_bar : 4;
    static const double beats[MOD_RATES] = { 0.25, 0.5, 1, 2, 1, 2, 4, 8, 16 };
    double per = m->rate < 0 || m->rate >= MOD_RATES ? bpb : m->rate < 4 ? beats[m->rate] : beats[m->rate] * bpb;
    double u = f->beat / per, ph = u - floor(u);
    switch (m->kind) {
    case MOD_RAMP:   return 2 * ph - 1;
    case MOD_SQUARE: return ph < 0.5 ? 1 : -1;
    case MOD_RANDOM: return pick(floor(u), 8 + j, 7, 1 << 16) / 32767.5 - 1;
    case MOD_KICK:   { double since = f->beat - s->last_kick; return since < 0 ? 0 : exp(-4 * since / per); }
    case MOD_LEVEL:  return f->sound ? f->level : 0;
    default:         return sin(ph * 2 * M_PI);
    }
}

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static int wrapi(int v, int n) { return ((v % n) + n) % n; }

void viz_seq_apply(viz_seq_t *s, const viz_seq_params_t *p, const viz_features_t *f, viz_look_t *L) {
    int bpb = f->beats_per_bar > 0 ? f->beats_per_bar : 4;
    viz_shape_knobs_t *k = &L->k;
    int rate7 = clampi(k->rate, 0, SHAPE_RATES - 1) + 2;

    /* the sequence */
    if (p->play) follow(s, p, f->beat, bpb);
    else { s->cur = -1; s->valid = 0; }
    if (s->cur >= 0) {
        const viz_seq_step_t *st = &p->step[s->cur];
        L->scene = st->scene == OPT_RANDOM ? s->draw[0] : viz_scene_order[clampi(st->scene - 1, 0, SCENE_COUNT - 1)];
        if (st->color == OPT_RANDOM) k->color = s->draw[1];
        else if (st->color != OPT_SAME) k->color = clampi(st->color - 2, 0, SHAPE_COLORS - 1);
        if (st->rate == OPT_RANDOM) rate7 = s->draw[2];
        else if (st->rate != OPT_SAME) rate7 = clampi(st->rate - 2, 0, SEQ_RATES - 1);
        if (st->size == OPT_RANDOM) k->size = s->draw[3];
        else if (st->size != OPT_SAME) k->size = clampi(st->size - 2, 0, SHAPE_SIZES - 1);
        if (st->fold == OPT_RANDOM) k->mirror = s->draw[4];
        else if (st->fold != OPT_SAME) k->mirror = clampi(st->fold - 2, 0, SHAPE_MIRRORS - 1);
    }

    /* the LFOs: an offset in steps on a knob with steps, a dip on LIGHT */
    if (f->sound && f->hits[0] != s->kicks) { s->kicks = f->hits[0]; s->last_kick = f->beat; }
    for (int j = 0; j < SEQ_MODS; j++) {
        const viz_mod_t *m = &p->mod[j];
        double v = lfo(s, m, j, f);
        s->mod_value[j] = (float)v;
        if (m->depth == 0) continue;
        int uni = m->kind == MOD_KICK || m->kind == MOD_LEVEL;
        if (m->aim == AIM_LIGHT) {
            double u = uni ? v : (v + 1) / 2;              /* 1: full, 0: the deepest dip */
            if (m->depth < 0) u = 1 - u;
            double g = 1 - fabs(m->depth) * (1 - u);
            L->light *= (float)(g < 0 ? 0 : g);
            continue;
        }
        static const int span[MOD_AIMS] = { 8, 8, 6, 3, 4, 4, 3, 4, 0, 3 };
        int off = (int)lrint(m->depth * v * span[m->aim < 0 || m->aim >= MOD_AIMS ? 0 : m->aim]);
        switch (m->aim) {
        case AIM_SCENE: L->scene = viz_scene_order[wrapi(viz_scene_pos[L->scene] + off, SCENE_COUNT)]; break;   /* along the menu */
        case AIM_COLOR: k->color = wrapi(k->color + off, SHAPE_COLORS); break;
        case AIM_RATE:  rate7 = clampi(rate7 + off, 0, SEQ_RATES - 1); break;
        case AIM_SIZE:  k->size = clampi(k->size + off, 0, SHAPE_SIZES - 1); break;
        case AIM_TRAIL: k->trail = clampi(k->trail + off, 0, SHAPE_TRAILS - 1); break;
        case AIM_FOLD:  k->mirror = clampi(k->mirror + off, 0, SHAPE_MIRRORS - 1); break;
        case AIM_SHIFT: k->shift = clampi(k->shift + off, 0, SHAPE_SHIFTS - 1); break;
        case AIM_REACT: k->react = clampi(k->react + off, 0, SHAPE_REACTS - 1); break;
        case AIM_TURN:  L->turn = wrapi(L->turn + off, 4); break;
        default: break;
        }
    }
    k->rate = clampi(rate7 - 2, 0, SHAPE_RATES - 1);
    k->steps_per_beat = rate7 < 2 ? k_rate7[rate7] : 0;
}
