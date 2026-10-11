/*
 * shapes.c — the hundred scenes (shapes.h).
 *
 * Each scene is a port of its draw() in tools/gallery/gallery.html. Keep the
 * two in step: change a scene there, judge it by eye, then port the change
 * here; tests/test_parity.c fails until they agree.
 *
 * Conventions carried over from the JavaScript, because they decide which
 * pad lights:
 *   - Math.round is floor(x + 0.5) (jround), not C's round();
 *   - % on a negative number keeps its sign in both languages, so the same
 *     ((v % P) + P) % P guards appear here as there;
 *   - Math.floor(a / b) is floor division (fdiv), never C's truncation;
 *   - the gallery's rng() and hash() are reproduced bit for bit, so a "random"
 *     choice is the same choice.
 */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "shapes.h"

#define CC 3.5
#define PI 3.141592653589793

const char *const viz_shape_names[SHAPE_COUNT] = {
    "Pulse", "Rings", "Diamond", "Burst", "Sweep", "Cross", "Snake", "Radar", "Stripes", "Tunnel",
    "Quads", "Shapes", "Steps", "Rise", "Bounce", "Wave", "Rain", "Bars", "Spiral", "Pinwheel",
    "Kit", "Icons", "Count", "Arrow", "Corners", "Stack", "Rocket", "Halves", "Pong", "Trace",
    "Invaders", "Breakout", "Text", "Spin", "Heart", "Sand", "Chomp", "Doors", "Flap", "Jump",
    "Noughts", "Four", "Road", "Maze", "Dice", "Catch", "Lander", "Tanks", "Face", "Eye",
    "Sun", "Moon", "Flower", "Tick", "Speaker", "Saturn", "Clock", "Charge", "UFO", "Wipe",
    "Curtain", "Iris", "Pie", "Diag", "Zoom", "Aim", "Propeller", "Eight", "Helix", "Metronome",
    "Cradle", "Piston", "Ping", "Juggle", "Frame", "Stairs", "Swap", "Bubbles", "Train", "Boxes",
    "Bass", "Pads", "Tally", "Beats", "Lift", "Hoop", "Bowl", "Darts", "Seesaw", "Yoyo",
    "Coil", "Volley", "Swing", "Goal", "Skate", "Mow", "Pour", "Ski", "Pool", "Weave",
};
const char *const viz_shape_color_names[SHAPE_COLORS] = {
    "Ice", "Fire", "Acid", "Sunset", "Mint", "Royal", "Candy", "Mono", "Rainbow"
};
const char *const viz_shape_rate_names[SHAPE_RATES] = { "1 bar", "1/2", "1/4", "1/8", "1/16" };
const char *const viz_shape_size_names[SHAPE_SIZES] = { "1", "2", "3", "4" };
const char *const viz_shape_trail_names[SHAPE_TRAILS] = { "Off", "Short", "Medium", "Long", "Smear" };
const char *const viz_shape_mirror_names[SHAPE_MIRRORS] = { "Off", "Sides", "Top", "Four", "Eight" };
const char *const viz_shape_shift_names[SHAPE_SHIFTS] = { "Hold", "Bar", "Beat", "Kick" };
const char *const viz_shape_react_names[SHAPE_REACTS] = { "Off", "Glow", "Grow", "Flash", "Flip" };

/* Three colours per set; Rainbow (the ninth) is computed. */
static const double k_pal[8][3][3] = {
    { {0, 90, 255}, {0, 220, 255}, {190, 240, 255} },     /* Ice */
    { {255, 25, 0}, {255, 130, 0}, {255, 225, 40} },      /* Fire */
    { {110, 255, 0}, {255, 0, 190}, {0, 255, 220} },      /* Acid */
    { {255, 0, 110}, {255, 120, 0}, {140, 0, 255} },      /* Sunset */
    { {0, 255, 110}, {0, 150, 255}, {210, 255, 235} },    /* Mint */
    { {120, 0, 255}, {255, 185, 0}, {0, 70, 255} },       /* Royal */
    { {255, 70, 170}, {60, 200, 255}, {255, 235, 70} },   /* Candy */
    { {255, 255, 255}, {140, 140, 150}, {70, 70, 80} },   /* Mono */
};
static const double k_rates[SHAPE_RATES] = { 0.25, 0.5, 1, 2, 4 };       /* steps per beat */
static const double k_trails[SHAPE_TRAILS] = { 0, 0.06, 0.15, 0.35, 0.9 };  /* seconds */

/* ---- the gallery's helpers ------------------------------------------------ */

static double jround(double x) { return floor(x + 0.5); }
static long fdiv(long a, long b) { return (long)floor((double)a / (double)b); }
static double cheb(double x, double y) { return fmax(fabs(x - CC), fabs(y - CC)); }
static double manh(double x, double y) { return fabs(x - CC) + fabs(y - CC); }
static long tri(long n, long m) { long p = n % (2 * m); return p < m ? p : 2 * m - p; }
static double dmin(double a, double b) { return a < b ? a : b; }
static double dmax(double a, double b) { return a > b ? a : b; }

typedef struct { uint32_t s; } rng_t;
/* JavaScript's ToUint32, for the integers the gallery feeds its generators. */
static uint32_t ju32(double d) { return (uint32_t)(int64_t)trunc(d); }
static rng_t rng(uint32_t seed) { rng_t r = { seed ? seed : 1 }; return r; }
static double rnext(rng_t *r) {
    uint32_t s = r->s;
    s ^= s << 13; s ^= s >> 17; s ^= s << 5;
    r->s = s;
    return s / 4294967296.0;
}
static rng_t hash(double n) { return rng(ju32(floor(n * 2654435761.0 + 977))); }
static double hash1(double n) { rng_t r = hash(n); return rnext(&r); }

static int g_spiral[64][2], g_hilbert[64][2], g_ready;

static void tables(void) {
    if (g_ready) return;
    int n = 0, x0 = 0, y0 = 0, x1 = 7, y1 = 7;
    while (x0 <= x1 && y0 <= y1) {
        for (int x = x0; x <= x1; x++) { g_spiral[n][0] = x; g_spiral[n][1] = y0; n++; }
        for (int y = y0 + 1; y <= y1; y++) { g_spiral[n][0] = x1; g_spiral[n][1] = y; n++; }
        if (y1 > y0) for (int x = x1 - 1; x >= x0; x--) { g_spiral[n][0] = x; g_spiral[n][1] = y1; n++; }
        if (x1 > x0) for (int y = y1 - 1; y > y0; y--) { g_spiral[n][0] = x0; g_spiral[n][1] = y; n++; }
        x0++; y0++; x1--; y1--;
    }
    for (int d = 0; d < 64; d++) {
        int x = 0, y = 0, t = d;
        for (int s = 1; s < 8; s *= 2) {
            int rx = 1 & (t >> 1), ry = 1 & (t ^ rx);
            if (ry == 0) {
                if (rx == 1) { x = s - 1 - x; y = s - 1 - y; }
                int tmp = x; x = y; y = tmp;
            }
            x += s * rx; y += s * ry; t /= 4;
        }
        g_hilbert[d][0] = x; g_hilbert[d][1] = y;
    }
    g_ready = 1;
}

/* The square ring k (0 innermost, 3 the border), clockwise from its top left. */
static int ring_path(int k, int out[][2]) {
    int lo = 3 - k, hi = 4 + k, n = 0;
    for (int x = lo; x <= hi; x++) { out[n][0] = x; out[n][1] = lo; n++; }
    for (int y = lo + 1; y <= hi; y++) { out[n][0] = hi; out[n][1] = y; n++; }
    for (int x = hi - 1; x >= lo; x--) { out[n][0] = x; out[n][1] = hi; n++; }
    for (int y = hi - 1; y > lo; y--) { out[n][0] = lo; out[n][1] = y; n++; }
    return n;
}

/* ---- drawing ------------------------------------------------------------- */

typedef struct {
    float *cur;
    int color;
    long sh;        /* SHIFT: how far the colours have rotated */
} G;

typedef struct {
    double beats, time, dt, f;
    long step;
    int s;
    int kick, snare, hat;
    const viz_shape_feat_t *ft;
} Cx;

static void hsv(double h, double out[3]) {
    h = fmod(fmod(h, 1) + 1, 1);
    int i = (int)floor(h * 6);
    if (i > 5) i = 5;
    double f = h * 6 - i, q = 1 - f;
    static const int pick[6][3] = { {0, 1, 2}, {3, 0, 2}, {2, 0, 1}, {2, 3, 0}, {1, 2, 0}, {0, 2, 3} };
    double v[4] = { 1, f, 0, q };   /* the gallery's [1, f, 0], [q, 1, 0], ... */
    for (int k = 0; k < 3; k++) out[k] = v[pick[i][k]] * 255;
}

static void colour(const G *g, int ci, int x, int y, double out[3]) {
    if (g->color >= 8) { hsv(ci / 3.0 + g->sh / 8.0 + (x + y) / 28.0, out); return; }
    const double *c = k_pal[g->color < 0 ? 0 : g->color][(ci + g->sh) % 3];
    out[0] = c[0]; out[1] = c[1]; out[2] = c[2];
}

static void set(G *g, double fx, double fy, int ci, double a) {
    double rx = jround(fx), ry = jround(fy);
    if (!(rx >= 0 && rx <= 7 && ry >= 0 && ry <= 7) || a <= 0) return;
    int x = (int)rx, y = (int)ry, k = (y * 8 + x) * 3;
    double col[3];
    colour(g, ci, x, y, col);
    double m = a < 1 ? a : 1;
    for (int j = 0; j < 3; j++) g->cur[k + j] = (float)dmax(g->cur[k + j], col[j] * m);
}

static void spr(G *g, const char *const *rows, int nrows, double ox, double oy, int ci, double a) {
    for (int y = 0; y < nrows; y++)
        for (int x = 0; rows[y][x]; x++)
            if (rows[y][x] == '#') set(g, ox + x, oy + y, ci, a);
}

static void line(G *g, double fx0, double fy0, double fx1, double fy1, int ci, double a) {
    long x0 = (long)jround(fx0), y0 = (long)jround(fy0), x1 = (long)jround(fx1), y1 = (long)jround(fy1);
    long dx = labs(x1 - x0), dy = -labs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, e = dx + dy;
    for (int i = 0; i < 64; i++) {
        set(g, x0, y0, ci, a);
        if (x0 == x1 && y0 == y1) break;
        long e2 = 2 * e;
        if (e2 >= dy) { e += dy; x0 += sx; }
        if (e2 <= dx) { e += dx; y0 += sy; }
    }
}

#define EACH for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++)
#define SP (c->step + c->f)

/* Small fixed lists stand in for the gallery's arrays; a full list drops the
 * newest, which the gallery's rates never reach. */
#define LIST_MAX 64

/* ---- the scenes, in gallery order ---------------------------------------- */

static void s_pulse(G *g, const Cx *c, void *st) {
    (void)st;
    double a = pow(1 - c->f, 1.6), r = c->s == 4 ? 9 : c->s;
    EACH if (cheb(x, y) < r) set(g, x, y, c->step % 2, a);
}

static void s_rings(G *g, const Cx *c, void *st) {
    (void)st;
    long r = (long)floor(c->f * 4);
    EACH { long d = (long)floor(cheb(x, y)); if (d <= r && d > r - c->s) set(g, x, y, c->step % 3, 1); }
}

static void s_diamond(G *g, const Cx *c, void *st) {
    (void)st;
    double r = 1 + floor(c->f * 7);
    EACH { double d = manh(x, y); if (d <= r && d > r - c->s) set(g, x, y, c->step % 3, 1); }
}

typedef struct { rng_t r; int n; struct { double x, y, t; int ci; } b[LIST_MAX]; } burst_t;
static void i_burst(void *p) { burst_t *st = p; st->r = rng(7); }
static void s_burst(G *g, const Cx *c, void *p) {
    burst_t *st = p;
    if (c->kick && st->n < LIST_MAX) {
        double x = 2.5 + rnext(&st->r) * 2, y = 2.5 + rnext(&st->r) * 2;
        st->b[st->n].x = x; st->b[st->n].y = y; st->b[st->n].t = c->time; st->b[st->n].ci = 0; st->n++;
    }
    if (c->snare && st->n < LIST_MAX) {
        double x = rnext(&st->r) * 8, y = rnext(&st->r) * 8;
        st->b[st->n].x = x; st->b[st->n].y = y; st->b[st->n].t = c->time; st->b[st->n].ci = 1 + (st->n % 2); st->n++;
    }
    int m = 0;
    for (int i = 0; i < st->n; i++) if (c->time - st->b[i].t < 1.2) st->b[m++] = st->b[i];
    st->n = m;
    for (int i = 0; i < st->n; i++) {
        double r = (c->time - st->b[i].t) * 12;
        EACH { double d = hypot(x + .5 - st->b[i].x, y + .5 - st->b[i].y);
               if (fabs(d - r) < c->s * .5 + .25) set(g, x, y, st->b[i].ci, 1); }
    }
}

static void s_sweep(G *g, const Cx *c, void *st) {
    (void)st;
    long m = 8 - c->s, p = tri(c->step, m), vert = (c->step / (2 * m)) % 2;
    EACH { int v = vert ? y : x; if (v >= p && v < p + c->s) set(g, x, y, (c->step / m) % 2, 1); }
}

static void s_cross(G *g, const Cx *c, void *st) {
    (void)st;
    int s = c->s < 3 ? c->s : 3, plus = c->step % 2 == 0;
    EACH {
        int on = plus ? (fabs(x - CC) <= s - .5 || fabs(y - CC) <= s - .5)
                      : (abs(x - y) <= s - 1 || abs(x + y - 7) <= s - 1);
        if (on) set(g, x, y, plus ? 0 : 1, 1);
    }
}

static void s_snake(G *g, const Cx *c, void *st) {
    (void)st;
    long head = (long)floor(SP * 2), len = c->s * 4;
    for (long i = 0; i < len; i++) {
        long k = tri(head - i + 512, 63);
        set(g, g_spiral[k][0], g_spiral[k][1], i == 0 ? 2 : 0, 1 - (double)i / (len + 2));
    }
}

static void s_radar(G *g, const Cx *c, void *st) {
    (void)st;
    double a = SP * PI / 2;
    EACH {
        double ang = atan2(y - CC, x - CC), d = fmod(a - ang, 2 * PI);
        if (d < 0) d += 2 * PI;
        if (d < .35 * c->s) set(g, x, y, (c->step / 4) % 2, 1 - d / (.35 * c->s) * .6);
    }
}

static void s_stripes(G *g, const Cx *c, void *st) {
    (void)st;
    EACH { long v = x + y + c->step, P = 2 * c->s; if (v % P < c->s) set(g, x, y, (v / P) % 2, 1); }
}

static void s_tunnel(G *g, const Cx *c, void *st) {
    (void)st;
    EACH {
        long v = (long)floor(cheb(x, y)) - c->step, P = c->s + 1, m = ((v % P) + P) % P;
        if (m == 0) set(g, x, y, (int)(((fdiv(v, P) % 3) + 3) % 3), 1);
    }
}

static void s_quads(G *g, const Cx *c, void *st) {
    (void)st;
    int q = c->step % 4, qx = q == 1 || q == 2, qy = q >= 2;
    EACH if ((x >= 4) == qx && (y >= 4) == qy && cheb(x, y) < c->s) set(g, x, y, q % 3, 1);
}

static void s_shapes(G *g, const Cx *c, void *st) {
    (void)st;
    int k = c->step % 6;
    double R = .5 + c->s * .78;
    EACH {
        double dx = fabs(x - CC), dy = fabs(y - CC), ch = dmax(dx, dy), mh = dx + dy, e = hypot(dx, dy);
        int on;
        if (k == 0) on = ch <= R && ch > R - 1;
        else if (k == 1) on = mh <= R * 1.6 && mh > R * 1.6 - 1;
        else if (k == 2) on = fabs(dx - dy) < .5 && ch <= R;
        else if (k == 3) on = (dx < .6 || dy < .6) && ch <= R;
        else if (k == 4) on = fabs(e - R) < .55;
        else on = ch <= R;
        if (on) set(g, x, y, k % 3, 1);
    }
}

static void s_steps(G *g, const Cx *c, void *st) {
    (void)st;
    long n = (long)floor(c->beats * 4), cur = n % 16, base = n - cur;
    for (int i = 0; i < 16; i++) {
        int bx = (i % 4) * 2, by = (i / 4) * 2, h = c->ft->log[(base + i) % 64];
        int ci = -1;
        double a = 0;
        if (i == cur) { ci = 0; a = 1; }
        else if (i < cur && (h & HIT_KICK)) { ci = 1; a = .8; }
        else if (i < cur) { ci = 2; a = .15; }
        if (ci >= 0) for (int k = 0; k < 4; k++) set(g, bx + (k % 2), by + (k >> 1), ci, a);
    }
}

static void s_rise(G *g, const Cx *c, void *st) {
    (void)st;
    long per = (long)ceil(8.0 / c->s), k = c->step % (per + 1);
    EACH {
        if (k == per) set(g, x, y, 2, 1 - c->f);
        else if (7 - y < (k + 1) * c->s) set(g, x, y, (7 - y) < 3 ? 0 : (7 - y) < 6 ? 1 : 2, 1);
    }
}

static void s_bounce(G *g, const Cx *c, void *st) {
    (void)st;
    int s = c->s < 3 ? c->s : 3;
    double h = 1 - pow(2 * c->f - 1, 2);
    long top = (long)jround((8 - s) * (1 - h)), lx = tri(c->step, 8 - s);
    for (long y = top; y < top + s; y++)
        for (long x = lx; x < lx + s; x++) set(g, x, y, c->step % 3, 1);
}

static void s_wave(G *g, const Cx *c, void *st) {
    (void)st;
    double A = 1.2 + 2.3 * c->ft->bass, ph = SP * PI / 2;
    EACH {
        double yc = CC + A * sin(x * PI / 4 + ph);
        if (fabs(y - yc) < c->s * .5 + .1) set(g, x, y, fabs(y - yc) < .5 ? 1 : 0, 1);
    }
}

typedef struct { rng_t r; int n; struct { int x, ci; double b; } d[LIST_MAX]; } rain_t;
static void i_rain(void *p) { rain_t *st = p; st->r = rng(3); }
static void s_rain(G *g, const Cx *c, void *p) {
    rain_t *st = p;
    if (c->hat)
        for (int i = 0; i < c->s; i++) {
            int x = (int)floor(rnext(&st->r) * 8);
            double b = SP;
            int ci = (int)floor(rnext(&st->r) * 3);
            if (st->n < LIST_MAX) { st->d[st->n].x = x; st->d[st->n].b = b; st->d[st->n].ci = ci; st->n++; }
        }
    int m = 0;
    for (int i = 0; i < st->n; i++) if ((SP - st->d[i].b) * 4 < 11) st->d[m++] = st->d[i];
    st->n = m;
    for (int i = 0; i < st->n; i++) {
        long y = (long)floor((SP - st->d[i].b) * 4);
        for (int k = 0; k < 3; k++)
            if (y - k >= 0 && y - k < 8) set(g, st->d[i].x, y - k, st->d[i].ci, 1 - k * .35);
    }
}

static void s_bars(G *g, const Cx *c, void *st) {
    (void)st;
    int w = c->s == 3 ? 4 : c->s == 4 ? 8 : c->s, n = 8 / w;
    for (int i = 0; i < n; i++) {
        double v = 0;
        for (int k = i * w; k < (i + 1) * w; k++) v = dmax(v, c->ft->bands[k]);
        long h = (long)jround(v * 8);
        for (int x = i * w; x < (i + 1) * w; x++)
            for (long r = 0; r < h; r++) set(g, x, 7 - r, r < 3 ? 0 : r < 6 ? 1 : 2, 1);
    }
}

static void s_spiral(G *g, const Cx *c, void *st) {
    (void)st;
    double ph = SP / 4;
    EACH {
        double u = atan2(y - CC, x - CC) / (2 * PI) + hypot(x - CC, y - CC) * .14 - ph, v = u * 2;
        double fr = fmod(fmod(v, 1) + 1, 1);
        long fv = (long)floor(v);
        if (fr < .12 + c->s * .1) set(g, x, y, (int)(((fv % 2) + 2) % 2), 1);
    }
}

static void s_pinwheel(G *g, const Cx *c, void *st) {
    (void)st;
    int n = c->s + 1;
    double a = c->step * PI / 8;
    EACH {
        double u = (atan2(y - CC, x - CC) - a) / (2 * PI);
        u = fmod(fmod(u, 1) + 1, 1);
        long k = (long)floor(u * 2 * n);
        if (k % 2 == 0) set(g, x, y, (int)((k / 2) % 3), 1);
    }
}

static void s_kit(G *g, const Cx *c, void *st) {
    (void)st;
    double r = .5 + c->s * .75;
    EACH {
        double d = cheb(x, y);
        if (d < r) set(g, x, y, 0, c->ft->kick);
        if (fabs(x - CC) > 2 && fabs(y - CC) > 2) set(g, x, y, 1, c->ft->snare);
        else if (d > 3) set(g, x, y, 2, c->ft->hat * .9);
    }
}

static const char *const k_icons[5][8] = {
    { ".##..##.", "########", "########", "########", ".######.", "..####..", "...##...", "........" },
    { "..####..", ".#....#.", "#.#..#.#", "#......#", "#.#..#.#", "#..##..#", ".#....#.", "..####.." },
    { "..#..#..", "#..##..#", "#.####.#", "###..###", "########", ".######.", ".#....#.", "#......#" },
    { "....###.", "....#.##", "....#...", "....#...", "..###...", ".####...", ".####...", "..##...." },
    { "...##...", "...##...", "########", ".######.", "..####..", ".######.", ".##..##.", "##....##" },
};
static void s_icons(G *g, const Cx *c, void *st) {
    (void)st;
    int k = c->step % 5;
    EACH if (k_icons[k][y][x] == '#') set(g, x, y, k % 3, 1);
}

static const char *const k_digits[9][7] = {
    { 0 },
    { "..#..", ".##..", "..#..", "..#..", "..#..", "..#..", ".###." },
    { ".###.", "#...#", "....#", "...#.", "..#..", ".#...", "#####" },
    { "####.", "....#", "....#", ".###.", "....#", "....#", "####." },
    { "...#.", "..##.", ".#.#.", "#..#.", "#####", "...#.", "...#." },
    { "#####", "#....", "####.", "....#", "....#", "#...#", ".###." },
    { ".###.", "#....", "#....", "####.", "#...#", "#...#", ".###." },
    { "#####", "....#", "...#.", "..#..", ".#...", ".#...", ".#..." },
    { ".###.", "#...#", "#...#", ".###.", "#...#", "#...#", ".###." },
};
static void s_count(G *g, const Cx *c, void *st) {
    (void)st;
    int n = (int)(c->step % 8) + 1;
    for (int y = 0; y < 7; y++)
        for (int x = 0; x < 5; x++)
            if (k_digits[n][y][x] == '#') set(g, x + 2, y + 1, n == 1 ? 2 : (n - 1) % 2, 1);
}

static const char *const k_arrow[8] = { "...##...", "..####..", ".######.", "########", "...##...", "...##...", "...##...", "...##..." };
static void s_arrow(G *g, const Cx *c, void *st) {
    (void)st;
    int d = c->step % 4, sh = (int)floor(c->f * 8);
    EACH {
        int u, v;
        if (d == 0) { u = x; v = y; } else if (d == 1) { u = y; v = 7 - x; }
        else if (d == 2) { u = 7 - x; v = 7 - y; } else { u = 7 - y; v = x; }
        v = (v + sh) % 8;
        if (k_arrow[v][u] == '#') set(g, x, y, d % 2, 1);
    }
}

static void s_corners(G *g, const Cx *c, void *st) {
    (void)st;
    long ring = 3 - (long)floor(c->f * 4);
    EACH {
        double dx = fabs(x - CC), dy = fabs(y - CC);
        if ((long)floor(cheb(x, y)) == ring && fabs(dx - dy) < c->s) set(g, x, y, c->step % 3, 1);
    }
}

typedef struct { rng_t r; int8_t occ[64]; int has, bx, bw, by, bci; long last; double flash; } tower_t;
static void i_stack(void *p) {
    tower_t *st = p;
    st->r = rng(31); memset(st->occ, -1, sizeof st->occ); st->last = -1; st->flash = -9;
}
static int stack_land(const tower_t *st, int x0, int w) {
    int y = 0;
    for (;;) {
        if (!(y + 1 < 8)) break;
        int free = 1;
        for (int i = 0; i < w; i++) if (st->occ[(y + 1) * 8 + x0 + i] >= 0) free = 0;
        if (!free) break;
        y++;
    }
    return y;
}
static void s_stack(G *g, const Cx *c, void *p) {
    tower_t *st = p;
    if (c->step != st->last) {
        st->last = c->step;
        if (st->has) {
            for (int i = 0; i < st->bw; i++) st->occ[st->by * 8 + st->bx + i] = (int8_t)st->bci;
            for (int y = 7; y >= 0; y--) {
                int full = 1;
                for (int x = 0; x < 8; x++) if (st->occ[y * 8 + x] < 0) full = 0;
                if (full) {
                    for (int yy = y; yy > 0; yy--) for (int x = 0; x < 8; x++) st->occ[yy * 8 + x] = st->occ[(yy - 1) * 8 + x];
                    for (int x = 0; x < 8; x++) st->occ[x] = -1;
                    y++;
                }
            }
        }
        int w = c->s + 1 < 4 ? c->s + 1 : 4, x = (int)floor(rnext(&st->r) * (9 - w)), y = stack_land(st, x, w);
        if (st->occ[x] >= 0 || y == 0) { memset(st->occ, -1, sizeof st->occ); st->flash = c->time; st->has = 0; }
        else { st->has = 1; st->bx = x; st->bw = w; st->by = y; st->bci = (int)floor(rnext(&st->r) * 3); }
    }
    if (c->time - st->flash < .3) EACH set(g, x, y, 2, 1 - (c->time - st->flash) / .3);
    EACH { int v = st->occ[y * 8 + x]; if (v >= 0) set(g, x, y, v, .8); }
    if (st->has) {
        double y = dmin(st->by, floor(c->f * 2.5 * (st->by + 1)));
        for (int i = 0; i < st->bw; i++) set(g, st->bx + i, y, st->bci, 1);
    }
}

static void s_rocket(G *g, const Cx *c, void *st) {
    (void)st;
    rng_t r = rng(ju32(c->step * 2654435761.0 + 1));
    double x = 1 + floor(rnext(&r) * 6), ty = 1 + floor(rnext(&r) * 3);
    int ci = c->step % 3;
    if (c->f < .45) {
        double y = 7 - floor((7 - ty) * c->f / .45);
        set(g, x, y, 2, 1); set(g, x, y + 1, 2, .4);
    } else {
        double R = (c->f - .45) / .55 * (1.5 + c->s);
        for (int py = 0; py < 8; py++)
            for (int px = 0; px < 8; px++) {
                double d = hypot(px - x, py - ty);
                if (fabs(d - R) < .6) set(g, px, py, ci, 1 - (c->f - .45) * .8);
            }
    }
}

static void s_halves(G *g, const Cx *c, void *st) {
    (void)st;
    long n = c->s + 1, k = c->step % n, vert = (c->step / n) % 2;
    EACH { int v = vert ? y : x; if ((long)floor(v * n / 8.0) == k) set(g, x, y, k % 3, 1); }
}

static void s_pong(G *g, const Cx *c, void *st) {
    (void)st;
    long n = (long)floor(SP * 4), bx = 1 + tri(n, 5), by = tri(n + 3, 7), L = c->s + 1;
    long py = by - fdiv(L, 2);
    if (py > 8 - L) py = 8 - L;
    if (py < 0) py = 0;
    for (long i = 0; i < L; i++) { set(g, 0, py + i, 0, 1); set(g, 7, py + i, 1, 1); }
    set(g, bx, by, 2, 1);
}

typedef struct { int h[8]; int q[16]; int qn; long n; } trace_t;
static void i_trace(void *p) { trace_t *st = p; for (int i = 0; i < 8; i++) st->h[i] = 5; st->n = -1; }
static void s_trace(G *g, const Cx *c, void *p) {
    trace_t *st = p;
    if (c->kick && st->qn < 4) { st->q[st->qn++] = 3; st->q[st->qn++] = 0; st->q[st->qn++] = 7; st->q[st->qn++] = 5; }
    long n = (long)floor(SP * 8);
    if (st->n < 0) st->n = n;
    while (st->n < n) {
        st->n++;
        memmove(st->h, st->h + 1, 7 * sizeof st->h[0]);
        if (st->qn) { st->h[7] = st->q[0]; memmove(st->q, st->q + 1, (size_t)(st->qn - 1) * sizeof st->q[0]); st->qn--; }
        else st->h[7] = 5;
    }
    for (int x = 0; x < 8; x++) {
        int a = st->h[x], b = x ? st->h[x - 1] : a, lo = a < b ? a : b, hi = a > b ? a : b;
        for (int y = lo; y <= hi; y++) set(g, x, y, y == a ? (a == 5 ? 0 : 2) : 1, 1);
    }
}

typedef struct { int n; struct { long x; double t; } s[LIST_MAX]; } shots_t;
static void s_invaders(G *g, const Cx *c, void *p) {
    shots_t *st = p;
    static const char *const fa[2] = { "###", ".#." }, *const fb[2] = { "###", "#.#" };
    const char *const *fr = c->step % 2 ? fb : fa;
    long ox = c->step % 2, oy = (c->step / 4) % 3;
    static const int at[4][2] = { {0, 0}, {4, 0}, {0, 3}, {4, 3} };
    for (int k = 0; k < 4; k++)
        for (int y = 0; y < 2; y++)
            for (int x = 0; x < 3; x++)
                if (fr[y][x] == '#') set(g, at[k][0] + x + ox, at[k][1] + y + oy, at[k][1] ? 1 : 0, 1);
    long px = 1 + tri((long)floor(SP * 2), 5);
    set(g, px, 7, 2, 1); set(g, px - 1, 7, 2, 1); set(g, px + 1, 7, 2, 1); set(g, px, 6, 2, 1);
    if (c->kick && st->n < LIST_MAX) { st->s[st->n].x = px; st->s[st->n].t = c->time; st->n++; }
    int m = 0;
    for (int i = 0; i < st->n; i++) if (c->time - st->s[i].t < .3) st->s[m++] = st->s[i];
    st->n = m;
    for (int i = 0; i < st->n; i++) set(g, st->s[i].x, 5 - floor((c->time - st->s[i].t) / .3 * 6), 2, 1);
}

typedef struct { uint8_t b[16]; int x, y, vx, vy; long n; } breakout_t;
static void i_breakout(void *p) {
    breakout_t *st = p;
    memset(st->b, 1, sizeof st->b); st->x = 3; st->y = 5; st->vx = 1; st->vy = -1; st->n = -1;
}
static void s_breakout(G *g, const Cx *c, void *p) {
    breakout_t *st = p;
    long n = (long)floor(SP * 2);
    if (st->n < 0) st->n = n;
    while (st->n < n) {
        st->n++;
        if (st->x + st->vx < 0 || st->x + st->vx > 7) st->vx = -st->vx;
        if (st->y + st->vy < 0 || st->y + st->vy > 6) st->vy = -st->vy;
        int nx = st->x + st->vx, ny = st->y + st->vy;
        if (ny < 2 && st->b[ny * 8 + nx]) { st->b[ny * 8 + nx] = 0; st->vy = -st->vy; }
        else { st->x = nx; st->y = ny; }
        int any = 0;
        for (int i = 0; i < 16; i++) any |= st->b[i];
        if (!any) memset(st->b, 1, sizeof st->b);
    }
    for (int i = 0; i < 16; i++) if (st->b[i]) set(g, i % 8, i >> 3, i >> 3, 1);
    long w = c->s + 1, px = st->x - fdiv(w, 2);
    if (px > 8 - w) px = 8 - w;
    if (px < 0) px = 0;
    for (long i = 0; i < w; i++) set(g, px + i, 7, 2, 1);
    set(g, st->x, st->y, 2, 1);
}

/* VIZ in a 5x7 font, a blank column after each letter and five at the end. */
static void s_text(G *g, const Cx *c, void *st) {
    (void)st;
    static const char *const V[7] = { "#...#", "#...#", "#...#", "#...#", ".#.#.", ".#.#.", "..#.." };
    static const char *const I[7] = { "###", ".#.", ".#.", ".#.", ".#.", ".#.", "###" };
    static const char *const Z[7] = { "#####", "....#", "...#.", "..#..", ".#...", "#....", "#####" };
    static const char *const *const L[3] = { V, I, Z };
    int col_li[32], col_bits[32], n = 0;
    for (int li = 0; li < 3; li++) {
        int w = (int)strlen(L[li][0]);
        for (int x = 0; x < w; x++) {
            int bits = 0;
            for (int y = 0; y < 7; y++) if (L[li][y][x] == '#') bits |= 1 << y;
            col_li[n] = li; col_bits[n] = bits; n++;
        }
        col_li[n] = -1; n++;
    }
    for (int k = 0; k < 5; k++) col_li[n++] = -1;
    long off = (long)floor(SP * 2);
    for (int x = 0; x < 8; x++) {
        int i = (int)((x + off) % n);
        if (col_li[i] < 0) continue;
        for (int y = 0; y < 7; y++) if (col_bits[i] & (1 << y)) set(g, x, y + 1, col_li[i], 1);
    }
}

static void s_spin(G *g, const Cx *c, void *st) {
    (void)st;
    double a = SP * PI / 4, R = 1.6 + c->s * .45, ca = cos(a), sa = sin(a);
    EACH {
        double dx = x - CC, dy = y - CC, u = dx * ca + dy * sa, v = -dx * sa + dy * ca;
        if (fabs(dmax(fabs(u), fabs(v)) - R) < .5) set(g, x, y, (c->step / 2) % 3, 1);
    }
}

static void s_heart(G *g, const Cx *c, void *st) {
    (void)st;
    static const char *const big[8] = { ".##..##.", "########", "########", "########", ".######.", "..####..", "...##...", "........" };
    static const char *const small[8] = { "........", "........", "..#..#..", ".######.", ".######.", "..####..", "...##...", "........" };
    const char *const *h = c->f < .3 ? big : small;
    EACH if (h[y][x] == '#') set(g, x, y, c->f < .3 ? 0 : 1, 1);
}

static void s_sand(G *g, const Cx *c, void *st) {
    (void)st;
    long cyc = c->step / 16;
    double p = ((c->step % 16) + c->f) / 16;
    int top[32][2], bot[32][2], nt = 0, nb = 0;
    EACH {
        int in = y < 4 ? fabs(x - CC) <= 3.5 - y : fabs(x - CC) <= y - 3.5;
        if (!in) continue;
        if (y < 4) { top[nt][0] = x; top[nt][1] = y; nt++; } else { bot[nb][0] = x; bot[nb][1] = y; nb++; }
    }
    /* the gallery's stable sort: lowest row first, then nearest the middle */
    int (*lists[2])[2] = { top, bot };
    int counts[2] = { nt, nb };
    for (int l = 0; l < 2; l++) {
        int (*a)[2] = lists[l];
        for (int i = 1; i < counts[l]; i++) {
            int px = a[i][0], py = a[i][1], j = i - 1;
            while (j >= 0) {
                double cmp = (double)(py - a[j][1]);
                if (cmp == 0) cmp = fabs(a[j][0] - CC) - fabs(px - CC);
                if (!(cmp > 0)) break;   /* a[j] belongs after px */
                a[j + 1][0] = a[j][0]; a[j + 1][1] = a[j][1]; j--;
            }
            a[j + 1][0] = px; a[j + 1][1] = py;
        }
    }
    long keep = (long)jround((1 - p) * nt), fallen = nt - keep;
    int flip = cyc % 2;
#define PUT(X, Y, CI, A) set(g, (X), flip ? 7 - (Y) : (Y), (CI), (A))
    for (int i = 0; i < nt; i++) PUT(top[i][0], top[i][1], i < keep ? 1 : 2, i < keep ? 1 : .12);
    for (int i = 0; i < nb; i++) PUT(bot[i][0], bot[i][1], i < fallen ? 1 : 2, i < fallen ? 1 : .12);
    if (keep > 0) PUT(((long)floor(c->step + c->f * 4) % 2) ? 3 : 4, 4 + ((long)floor(c->f * 8) % 3), 0, 1);
#undef PUT
}

static void s_chomp(G *g, const Cx *c, void *st) {
    (void)st;
    static const char *const open[5] = { ".###.", "###..", "##...", "###..", ".###." };
    static const char *const shut[5] = { ".###.", "#####", "#####", "#####", ".###." };
    long px = ((long)floor(SP * 2) % 14) - 5;
    const char *const *sp = c->f < .5 ? open : shut;
    for (int x = 1; x < 8; x += 2) if (x > px + 3) set(g, x, 4, 2, 1);
    for (int y = 0; y < 5; y++)
        for (int x = 0; x < 5; x++)
            if (sp[y][x] == '#') set(g, px + x, y + 2, 0, 1);
}

static void s_doors(G *g, const Cx *c, void *st) {
    (void)st;
    long w = (long)floor(dmin(1, c->f * 1.6) * 4);
    int vert = c->step % 2;
    EACH { int v = vert ? y : x; if (v < 4 - w) set(g, x, y, 0, 1); else if (v >= 4 + w) set(g, x, y, 1, 1); }
}

typedef struct { double y; } flap_t;
static void i_flap(void *p) { ((flap_t *)p)->y = 3; }
static void s_flap(G *g, const Cx *c, void *p) {
    flap_t *st = p;
    st->y = dmax(0, dmin(7, st->y + c->dt * 4 - (c->kick ? 2.2 : 0)));
    long sc = (long)floor(SP * 2);
    for (int x = 0; x < 8; x++) {
        long X = x + sc;
        if (X % 4) continue;
        long gap = 1 + (long)floor(hash1(X / 4) * 4);
        for (int y = 0; y < 8; y++) if (y < gap || y > gap + 2) set(g, x, y, 0, 1);
    }
    set(g, 2, st->y, 2, 1);
}

typedef struct { double j; } jump_t;
static void i_jump(void *p) { ((jump_t *)p)->j = -9; }
static void s_jump(G *g, const Cx *c, void *p) {
    jump_t *st = p;
    if (c->kick) st->j = c->time;
    double age = c->time - st->j, h = age < .4 ? jround(3 * sin(PI * age / .4)) : 0;
    for (int x = 0; x < 8; x++) {
        set(g, x, 7, 0, .35);
        long X = x + (long)floor(SP * 2);
        if (X % 7 == 0) { set(g, x, 6, 1, 1); set(g, x, 5, 1, 1); }
    }
    set(g, 1, 6 - h, 2, 1); set(g, 1, 5 - h, 2, 1);
}

static void s_noughts(G *g, const Cx *c, void *st) {
    (void)st;
    long game = c->step / 11, k = c->step % 11;
    rng_t r = hash(game + 5);
    int o[9];
    for (int i = 0; i < 9; i++) o[i] = i;
    for (int i = 8; i > 0; i--) { int j = (int)floor(rnext(&r) * (i + 1)), t = o[i]; o[i] = o[j]; o[j] = t; }
    for (int i = 0; i < 8; i++) { set(g, 2, i, 2, .3); set(g, 5, i, 2, .3); set(g, i, 2, 2, .3); set(g, i, 5, 2, .3); }
    if (k == 10) return;
    for (long m = 0; m < (k + 1 < 9 ? k + 1 : 9); m++) {
        int cx = (o[m] % 3) * 3, cy = (o[m] / 3) * 3;
        for (int q = 0; q < 4; q++) set(g, cx + (q % 2), cy + (q >> 1), (int)(m % 2), 1);
    }
}

static void s_four(G *g, const Cx *c, void *st) {
    (void)st;
    long game = c->step / 50, k = c->step % 50;
    rng_t r = hash(game * 3 + 1);
    int h[8] = { 0 };
    if (k >= 48) { if (k == 48) EACH if (y >= 2) set(g, x, y, 2, 1 - c->f); return; }
    for (long m = 0; m <= k; m++) {
        int col;
        do col = (int)floor(rnext(&r) * 8); while (h[col] >= 6);
        int land = 7 - h[col];
        h[col]++;
        double y = m < k ? land : floor(dmin(1, c->f * 2) * land);
        set(g, col, y, (int)(m % 2), 1);
    }
}

static void s_road(G *g, const Cx *c, void *st) {
    (void)st;
    long n = (long)floor(SP * 4);
#define LEFT(Y) ((long)jround(3.5 + 1.6 * sin((double)(n - (Y)) * .35) - 2.5))
    for (int y = 0; y < 8; y++) {
        set(g, LEFT(y), y, 0, 1); set(g, LEFT(y) + 5, y, 0, 1);
        if (y < 6 && (n - y) % 3 == 0) set(g, LEFT(y) + 2, y, 2, .5);
    }
    long cx = LEFT(6) + 2;
#undef LEFT
    for (int q = 0; q < 4; q++) set(g, cx + (q % 2), 6 + (q >> 1), 1, 1);
}

static void s_maze(G *g, const Cx *c, void *st) {
    (void)st;
    EACH {
        int wall = y % 2 == 1 && !(y == 1 && x == 7) && !(y == 3 && x == 0) && !(y == 5 && x == 7);
        if (wall) set(g, x, y, 0, .35);
    }
    int path[40][2], n = 0;
    for (int r = 0; r < 4; r++) {
        for (int i = 0; i < 8; i++) { path[n][0] = r % 2 ? 7 - i : i; path[n][1] = r * 2; n++; }
        if (r < 3) { path[n][0] = r % 2 ? 0 : 7; path[n][1] = r * 2 + 1; n++; }
    }
    long k = tri((long)floor(SP * 2), n - 1);
    set(g, path[k][0], path[k][1], 2, 1);
}

static void s_dice(G *g, const Cx *c, void *st) {
    (void)st;
    static const int P[7][6][2] = {
        { {0} }, { {3, 3} }, { {0, 0}, {6, 6} }, { {0, 0}, {3, 3}, {6, 6} }, { {0, 0}, {6, 0}, {0, 6}, {6, 6} },
        { {0, 0}, {6, 0}, {3, 3}, {0, 6}, {6, 6} }, { {0, 0}, {6, 0}, {0, 3}, {6, 3}, {0, 6}, {6, 6} } };
    int rolling = c->f < .3;
    int v = 1 + (int)floor(hash1(rolling ? c->step * 50 + floor(c->f * 20) : c->step) * 6);
    for (int i = 0; i < v; i++)
        for (int q = 0; q < 4; q++)
            set(g, P[v][i][0] + (q % 2), P[v][i][1] + (q >> 1), rolling ? 2 : v % 3, rolling ? .5 : 1);
}

static void s_catch(G *g, const Cx *c, void *st) {
    (void)st;
    double x = floor(hash1(c->step) * 8), px = floor(hash1(c->step - 1) * 8), y = floor(c->f * 8);
    double bx = jround(px + (x - px) * dmin(1, c->f * 3));
    int caught = y >= 7;
    for (int i = -1; i <= 1; i++) set(g, dmax(1, dmin(6, bx)) + i, 7, caught ? 2 : 1, 1);
    if (!caught) set(g, x, y, 0, 1);
}

static void s_lander(G *g, const Cx *c, void *st) {
    (void)st;
    static const char *const L[2] = { ".##.", "#..#" };
    long k = c->step % 8, y = k < 5 ? k : 5;
    for (int x = 2; x < 6; x++) set(g, x, 7, k >= 5 ? 2 : 1, k == 5 ? 1 : .7);
    spr(g, L, 2, 2, y, 0, 1);
    if (k < 5 && c->ft->kick > .2) { set(g, 3, y + 2, 2, c->ft->kick); set(g, 4, y + 2, 2, c->ft->kick); }
}

typedef struct { int n; struct { int r; double t; } s[LIST_MAX]; } tanks_t;
static void s_tanks(G *g, const Cx *c, void *p) {
    tanks_t *st = p;
    static const char *const a[2] = { "..#", "###" }, *const b[2] = { "#..", "###" };
    spr(g, a, 2, 0, 6, 0, 1); spr(g, b, 2, 5, 6, 1, 1);
    if (c->kick && st->n < LIST_MAX) { st->s[st->n].r = 0; st->s[st->n].t = c->time; st->n++; }
    if (c->snare && st->n < LIST_MAX) { st->s[st->n].r = 1; st->s[st->n].t = c->time; st->n++; }
    int m = 0;
    for (int i = 0; i < st->n; i++) if (c->time - st->s[i].t < .5) st->s[m++] = st->s[i];
    st->n = m;
    for (int i = 0; i < st->n; i++) {
        double u = (c->time - st->s[i].t) / .5;
        set(g, st->s[i].r ? 5 - 4 * u : 2 + 4 * u, 5 - 4 * sin(PI * u), st->s[i].r, 1);
    }
}

static void s_face(G *g, const Cx *c, void *st) {
    (void)st;
    static const char *const F[4][8] = {
        { "..####..", ".#....#.", "#.#..#.#", "#......#", "#.#..#.#", "#..##..#", ".#....#.", "..####.." },
        { "..####..", ".#....#.", "#.#..#.#", "#......#", "#..##..#", "#.#..#.#", ".#....#.", "..####.." },
        { "..####..", ".#....#.", "#.#.##.#", "#......#", "#.#..#.#", "#..##..#", ".#....#.", "..####.." },
        { "..####..", ".#....#.", "#.#..#.#", "#......#", "#..##..#", "#..##..#", ".#....#.", "..####.." } };
    spr(g, F[c->step % 4], 8, 0, 0, c->step % 3, 1);
}

typedef struct { double b; } eye_t;
static void i_eye(void *p) { ((eye_t *)p)->b = -9; }
static void s_eye(G *g, const Cx *c, void *p) {
    eye_t *st = p;
    static const char *const E[6] = { "..####..", ".#....#.", "#......#", "#......#", ".#....#.", "..####.." };
    if (c->snare) st->b = c->time;
    if (c->time - st->b < .12) { for (int x = 1; x < 7; x++) set(g, x, 4, 0, 1); return; }
    spr(g, E, 6, 0, 1, 0, 1);
    static const int pxs[4] = { 2, 3, 4, 3 };
    int px = pxs[c->step % 4];
    for (int q = 0; q < 4; q++) set(g, px + (q % 2), 3 + (q >> 1), 2, 1);
}

static void s_sun(G *g, const Cx *c, void *st) {
    (void)st;
    EACH if (hypot(x - CC, y - CC) < 1.6) set(g, x, y, 0, 1);
    static const int diag[4][2] = { {0, 0}, {6, 0}, {0, 6}, {6, 6} }, axis[4][2] = { {3, 0}, {3, 6}, {0, 3}, {6, 3} };
    const int (*B)[2] = c->step % 2 ? diag : axis;
    for (int i = 0; i < 4; i++)
        for (int q = 0; q < 4; q++) set(g, B[i][0] + (q % 2), B[i][1] + (q >> 1), 1, 1);
}

static void s_moon(G *g, const Cx *c, void *st) {
    (void)st;
    double p = ((c->step % 16) + c->f) / 16, cs = cos(2 * PI * p);
    EACH {
        double dx = x - CC, dy = y - CC;
        if (hypot(dx, dy) > 3.7) continue;
        double w = sqrt(dmax(0, 13.7 - dy * dy));
        int lit = p < .5 ? dx > w * cs : dx < -w * cs;
        set(g, x, y, lit ? 2 : 0, lit ? 1 : .12);
    }
}

static void s_flower(G *g, const Cx *c, void *st) {
    (void)st;
    double L = 1 + floor(dmin(c->f * 2, 1) * 2);
    int n = c->step % 2 ? 8 : 4;
    EACH {
        double dx = x - CC, dy = y - CC, r = hypot(dx, dy), a = atan2(dy, dx), seg = 2 * PI / n;
        double diff = fabs(a - jround(a / seg) * seg);
        if (r < 1) set(g, x, y, 2, 1);
        else if (r <= 1 + L && diff < (n == 4 ? .45 : .3)) set(g, x, y, c->step % 2, 1);
    }
}

static void s_tick(G *g, const Cx *c, void *st) {
    (void)st;
    static const int tick[7][2] = { {1, 4}, {2, 5}, {3, 6}, {4, 5}, {5, 4}, {6, 3}, {7, 2} };
    static const int cross[12][2] = { {1, 1}, {2, 2}, {3, 3}, {4, 4}, {5, 5}, {6, 6}, {6, 1}, {5, 2}, {4, 3}, {3, 4}, {2, 5}, {1, 6} };
    int isT = c->step % 2 == 0, len = isT ? 7 : 12;
    const int (*pts)[2] = isT ? tick : cross;
    long n = (long)ceil(dmin(1, c->f * 1.5) * len);
    for (long i = 0; i < n; i++) {
        set(g, pts[i][0], pts[i][1], isT ? 0 : 1, 1);
        if (isT) set(g, pts[i][0], pts[i][1] - 1, 0, 1);
    }
}

typedef struct { double k; } speaker_t;
static void i_speaker(void *p) { ((speaker_t *)p)->k = -9; }
static void s_speaker(G *g, const Cx *c, void *p) {
    speaker_t *st = p;
    static const char *const S[6] = { "..##", "####", "####", "####", "####", "..##" };
    if (c->kick) st->k = c->time;
    double age = c->time - st->k;
    spr(g, S, 6, 0, 1, 0, 1);
    EACH {
        if (x < 4) continue;
        double r = hypot(x - 2.5, y - CC);
        for (int i = 1; i <= 3; i++)
            if (fabs(r - (1.3 + i * 1.3)) < .5 && age > (i - 1) * .06 && age < (i - 1) * .06 + .2)
                set(g, x, y, i == 1 ? 2 : 1, 1);
    }
}

static void s_saturn(G *g, const Cx *c, void *st) {
    (void)st;
    double a = c->step * PI / 8, ca = cos(a), sa = sin(a);
    EACH {
        double dx = x - CC, dy = y - CC, u = dx * ca + dy * sa, v = -dx * sa + dy * ca;
        int disc = hypot(dx, dy) < 1.9;
        double e = hypot(u / 3.6, v / 1.1);
        if (fabs(e - 1) < .25 && !(disc && v < 0)) set(g, x, y, 1, 1);
        else if (disc) set(g, x, y, 0, 1);
    }
}

static void clock_hand(G *g, long k, double len, int ci) {
    double a = k * PI / 4 - PI / 2;
    line(g, 3.5, 3.5, 3.5 + cos(a) * len, 3.5 + sin(a) * len, ci, 1);
}
static void s_clock(G *g, const Cx *c, void *st) {
    (void)st;
    static const int M[8][2] = { {3, 0}, {4, 0}, {7, 3}, {7, 4}, {3, 7}, {4, 7}, {0, 3}, {0, 4} };
    for (int i = 0; i < 8; i++) set(g, M[i][0], M[i][1], 2, .3);
    clock_hand(g, (c->step / 8) % 8, 2, 1);
    clock_hand(g, c->step % 8, 3.4, 0);
}

static void s_charge(G *g, const Cx *c, void *st) {
    (void)st;
    long k = c->step % 7;
    EACH if (y >= 1 && y <= 6 && x <= 6 && (y == 1 || y == 6 || x == 0 || x == 6)) set(g, x, y, 2, .45);
    set(g, 7, 3, 2, .45); set(g, 7, 4, 2, .45);
    for (int x = 1; x <= 5; x++)
        for (int y = 2; y <= 5; y++) {
            if (k == 6) set(g, x, y, 2, 1 - c->f);
            else if (x <= k) set(g, x, y, k < 2 ? 0 : 1, 1);
        }
}

static void s_ufo(G *g, const Cx *c, void *st) {
    (void)st;
    static const char *const U[3] = { "..##..", ".####.", "######" };
    long ox = tri(c->step, 2);
    spr(g, U, 3, ox, 0, 0, 1);
    if (c->ft->kick > .15)
        for (int y = 3; y < 8; y++) {
            double h = (y - 2) * .5;
            for (int x = 0; x < 8; x++) if (fabs(x - (ox + 2.5)) <= h) set(g, x, y, 2, c->ft->kick * .8);
        }
}

/* Wipes: the new colour is step % 3 over the old (step + 2) % 3. */
#define NEW_OLD(cond) set(g, x, y, (cond) ? (int)(c->step % 3) : (int)((c->step + 2) % 3), 1)

static void s_wipe(G *g, const Cx *c, void *st) {
    (void)st;
    int d = c->step % 4;
    long e = (long)floor(c->f * 9);
    EACH { int v = d == 0 ? x : d == 1 ? y : d == 2 ? 7 - x : 7 - y; NEW_OLD(v < e); }
}

static void s_curtain(G *g, const Cx *c, void *st) {
    (void)st;
    EACH { double t = dmax(0, dmin(1, (c->f - x * .07) / .4)); NEW_OLD(y < floor(t * 8)); }
}

static void s_iris(G *g, const Cx *c, void *st) {
    (void)st;
    double r = c->f * 5.2;
    EACH NEW_OLD(hypot(x - CC, y - CC) < r);
}

static void s_pie(G *g, const Cx *c, void *st) {
    (void)st;
    double a = c->f * 2 * PI;
    EACH { double t = fmod(atan2(x - CC, -(y - CC)) + 2 * PI, 2 * PI); NEW_OLD(t < a); }
}

static void s_diag(G *g, const Cx *c, void *st) {
    (void)st;
    int k = c->step % 4;
    long e = (long)floor(c->f * 16);
    EACH { int v = k == 0 ? x + y : k == 1 ? 7 - x + y : k == 2 ? 14 - x - y : x + 7 - y; NEW_OLD(v < e); }
}

static void s_zoom(G *g, const Cx *c, void *st) {
    (void)st;
    long r = 3 - (long)floor(c->f * 4);
    EACH { long d = (long)floor(cheb(x, y)); if (d >= r && d < r + c->s) set(g, x, y, c->step % 3, 1); }
}

static void s_aim(G *g, const Cx *c, void *st) {
    (void)st;
    long hy = tri((long)floor(SP * 2), 7), vx = tri((long)floor(SP * 1.5) + 3, 7);
    for (int i = 0; i < 8; i++) { set(g, i, hy, 0, 1); set(g, vx, i, 1, 1); }
    set(g, vx, hy, 2, 1);
}

static void s_propeller(G *g, const Cx *c, void *st) {
    (void)st;
    int n = c->s < 3 ? c->s : 3;
    for (int b = 0; b < n; b++) {
        double a = c->step * PI / 8 + b * PI / n;
        line(g, CC - cos(a) * 4, CC - sin(a) * 4, CC + cos(a) * 4, CC + sin(a) * 4, b % 3, 1);
    }
}

static void s_eight(G *g, const Cx *c, void *st) {
    (void)st;
    double t = SP * PI / 2;
    for (int i = 0; i < 4; i++) {
        double u = t - i * .18;
        set(g, CC + 3.4 * sin(u), CC + 3.2 * sin(2 * u), i ? 0 : 2, 1 - i * .22);
    }
}

static void s_helix(G *g, const Cx *c, void *st) {
    (void)st;
    double ph = SP * PI / 2;
    for (int x = 0; x < 8; x++) {
        double s = sin(x * PI / 4 + ph), y1 = jround(CC + 3 * s), y2 = jround(CC - 3 * s);
        if (x % 2 == 0) for (double y = dmin(y1, y2) + 1; y < dmax(y1, y2); y++) set(g, x, y, 2, .25);
        set(g, x, y1, 0, 1); set(g, x, y2, 1, 1);
    }
}

static void s_metronome(G *g, const Cx *c, void *st) {
    (void)st;
    double a = .62 * cos(PI * SP), tx = 3.5 + 7 * sin(a), ty = 7.4 - 7 * cos(a);
    line(g, 3.5, 7, tx, ty, 0, 1);
    set(g, 3.5 + 4 * sin(a), 7.4 - 4 * cos(a), 2, 1);
    set(g, 3, 7, 1, 1); set(g, 4, 7, 1, 1);
}

static void s_cradle(G *g, const Cx *c, void *st) {
    (void)st;
    double a = .8 * sin(PI * c->f);
    int left = c->step % 2 == 0;
    for (int x = 2; x <= 5; x++) {
        int sw = (left && x == 2) || (!left && x == 5);
        double bx = sw ? x + (left ? -1 : 1) * 6 * sin(a) : x, by = sw ? 6 * cos(a) : 6;
        line(g, x, 0, bx, by, 2, .2);
        set(g, bx, by, sw ? 1 : 0, 1);
    }
}

static void s_piston(G *g, const Cx *c, void *st) {
    (void)st;
    for (int i = 0; i < 4; i++) {
        double p = fabs(cos(PI * SP / 2 + i * PI / 4));
        long h = (long)jround(1 + 7 * p);
        for (int x = i * 2; x < i * 2 + 2; x++)
            for (long y = 8 - h; y < 8; y++) set(g, x, y, y == 8 - h ? 2 : i % 2, 1);
    }
}

static void s_ping(G *g, const Cx *c, void *st) {
    (void)st;
    int s = c->s < 3 ? c->s : 3, m = 8 - s;
    int K[4][2] = { {0, 0}, {m, m}, {m, 0}, {0, m} }, k = c->step % 4;
    const int *A = K[k], *B = K[(k + 1) % 4];
    double x = jround(A[0] + (B[0] - A[0]) * c->f), y = jround(A[1] + (B[1] - A[1]) * c->f);
    for (int j = 0; j < s; j++) for (int i = 0; i < s; i++) set(g, x + i, y + j, k % 3, 1);
}

static void s_juggle(G *g, const Cx *c, void *st) {
    (void)st;
    set(g, 1, 7, 2, .3); set(g, 6, 7, 2, .3);
    for (int i = 0; i < 3; i++) {
        double p = fmod(SP / 3 + i / 3.0, 1), u = p < .5 ? p * 2 : (p - .5) * 2, x = p < .5 ? 1 + 5 * u : 6 - 5 * u;
        set(g, x, 6.4 - 6 * sin(PI * u), i, 1);
    }
}

static void s_frame(G *g, const Cx *c, void *st) {
    (void)st;
    long k = c->step % 8, n = k < 4 ? k + 1 : 8 - k;
    EACH { long r = (long)floor(cheb(x, y)); if (3 - r < n) set(g, x, y, r % 2 ? 0 : 1, 1); }
}

static void s_stairs(G *g, const Cx *c, void *st) {
    (void)st;
    long k = c->step % 5;
    for (int i = 0; i < 4; i++) {
        if (k < 4 && i >= k + 1) continue;
        for (int x = i * 2; x < i * 2 + 2; x++)
            for (int y = 6 - i * 2; y < 8; y++) set(g, x, y, k == 4 ? 2 : i % 3, k == 4 ? 1 - c->f : 1);
    }
}

static void swap_sq(G *g, const int P[2], const int Q[2], double e, int ci) {
    double x = jround(P[0] + (Q[0] - P[0]) * e), y = jround(P[1] + (Q[1] - P[1]) * e);
    for (int j = 0; j < 3; j++) for (int i = 0; i < 3; i++) set(g, x + i, y + j, ci, 1);
}
static void s_swap(G *g, const Cx *c, void *st) {
    (void)st;
    double f = c->f, e = f < .5 ? 2 * f * f : 1 - 2 * (1 - f) * (1 - f);
    int d = c->step % 2;
    int A[2] = { d ? 5 : 0, 0 }, B[2] = { d ? 0 : 5, 5 };
    swap_sq(g, A, B, e, 0);
    swap_sq(g, B, A, e, 1);
}

typedef struct { long last; int n; struct { int x, ci; double t; } b[LIST_MAX]; } bubbles_t;
static void i_bubbles(void *p) { ((bubbles_t *)p)->last = -1; }
static void s_bubbles(G *g, const Cx *c, void *p) {
    bubbles_t *st = p;
    if (c->step != st->last) {
        st->last = c->step;
        if (st->n < LIST_MAX) {
            st->b[st->n].x = (int)floor(hash1(c->step) * 7); st->b[st->n].t = c->time; st->b[st->n].ci = c->step % 2; st->n++;
        }
    }
    int m = 0;
    for (int i = 0; i < st->n; i++) if (c->time - st->b[i].t < .85) st->b[m++] = st->b[i];
    st->n = m;
    for (int i = 0; i < st->n; i++) {
        double y = 7 - floor((c->time - st->b[i].t) * 10);
        int s = c->s > 1 ? 2 : 1, x = st->b[i].x;
        if (y <= 0) { set(g, x, 0, 2, 1); set(g, x - 1, 0, 2, .6); set(g, x + 1, 0, 2, .6); set(g, x, 1, 2, .6); }
        else for (int j = 0; j < s; j++) for (int k = 0; k < s; k++) set(g, x + k, y - j, st->b[i].ci, 1);
    }
}

static void s_train(G *g, const Cx *c, void *st) {
    (void)st;
    int R[32][2], n = ring_path(2, R);
    long h = (long)floor(SP * 2);
    for (int i = 0; i < n; i++) set(g, R[i][0], R[i][1], 2, .15);
    static const int J[6] = { 0, 1, 3, 4, 6, 7 };
    for (int i = 0; i < 6; i++) {
        long k = ((h - J[i]) % 20 + 20) % 20;
        set(g, R[k][0], R[k][1], J[i] < 2 ? 0 : 1, 1);
    }
}

static void box_outline(G *g, double x0, int ci) {
    for (int i = 0; i < 4; i++) {
        set(g, x0 + i, 2, ci, 1); set(g, x0 + i, 5, ci, 1);
        set(g, x0, 2 + i, ci, 1); set(g, x0 + 3, 2 + i, ci, 1);
    }
}
static void s_boxes(G *g, const Cx *c, void *st) {
    (void)st;
    double o = jround(2 * fabs(sin(PI * SP / 2)));
    box_outline(g, 2 - o, 0);
    box_outline(g, 2 + o, 1);
}

static void s_bass(G *g, const Cx *c, void *st) {
    (void)st;
    long h = (long)jround(((double)c->ft->bands[0] + (double)c->ft->bands[1]) / 2 * 8);
    for (int x = 0; x < 8; x++) for (long r = 0; r < h; r++) set(g, x, 7 - r, r == h - 1 ? 2 : 0, 1);
}

typedef struct { int h; } pads_t;
static void s_pads(G *g, const Cx *c, void *p) {
    pads_t *st = p;
    if (c->hat) st->h = (st->h + 1) % 4;
    const viz_shape_feat_t *ft = c->ft;
    for (int i = 0; i < 16; i++) {
        int px = (i % 4) * 2, py = (i / 4) * 2, ci = 2;
        double a = .1;
        if (i >= 12 && i < 14) { ci = 0; a = dmax(a, ft->kick); }
        else if (i >= 14) { ci = 1; a = dmax(a, ft->snare); }
        else if (i >= 8) { ci = 0; a = dmax(a, ft->bass * (i == 8 + ((long)floor(c->beats) % 4) ? 1 : .3)); }
        else if (i == st->h) a = dmax(a, ft->hat * 3);
        for (int q = 0; q < 4; q++) set(g, px + (q % 2), py + (q >> 1), ci, dmin(1, a));
    }
}

static void s_tally(G *g, const Cx *c, void *st) {
    (void)st;
    long n16 = (long)floor(c->beats * 4), base = n16 - (n16 % 64);
    int k = 0;
    for (long i = base; i <= n16 && k < 64; i++) {
        int h = c->ft->log[i % 64];
        static const int bit[3] = { HIT_KICK, HIT_SNARE, HIT_HAT };
        for (int j = 0; j < 3; j++)
            if ((h & bit[j]) && k < 64) { set(g, k % 8, k / 8, j, 1); k++; }
    }
}

static void s_beats(G *g, const Cx *c, void *st) {
    (void)st;
    long b = (long)floor(c->beats) % 4;
    double fr = c->beats - floor(c->beats);
    for (int i = 0; i < 4; i++)
        for (int x = i * 2; x < i * 2 + 2; x++)
            for (int y = 0; y < 8; y++) {
                if (i == b) { if (y >= floor(fr * 8)) set(g, x, y, i == 0 ? 2 : 0, 1); }
                else set(g, x, y, 1, .1);
            }
}

static void s_lift(G *g, const Cx *c, void *st) {
    (void)st;
    long f0 = tri(c->step, 3), f1 = tri(c->step + 1, 3);
    double y = jround(6 - 2 * (f0 + (f1 - f0) * dmin(1, c->f * 2)));
    for (int k = 0; k < 4; k++) { set(g, 0, k * 2 + 1, 2, .3); set(g, 7, k * 2 + 1, 2, .3); }
    for (int x = 2; x < 6; x++) { set(g, x, y, 0, 1); set(g, x, y + 1, 0, 1); }
}

typedef struct { int n; double t[LIST_MAX]; } hoop_t;
static void s_hoop(G *g, const Cx *c, void *p) {
    hoop_t *st = p;
    for (int y = 0; y < 3; y++) set(g, 7, y, 2, .4);
    set(g, 5, 2, 1, 1); set(g, 6, 2, 1, 1);
    if (c->kick && st->n < LIST_MAX) st->t[st->n++] = c->time;
    int m = 0;
    for (int i = 0; i < st->n; i++) if (c->time - st->t[i] < .6) st->t[m++] = st->t[i];
    st->n = m;
    for (int i = 0; i < st->n; i++) {
        double u = (c->time - st->t[i]) / .45;
        if (u <= 1) set(g, .5 + 5 * u, 6 - 4 * u - 3 * sin(PI * u), 0, 1);
        else {
            double y = 2 + (u - 1) * 14;
            set(g, 5.5, y, 0, 1);
            if (y < 5) { set(g, 5, 3, 2, 1); set(g, 6, 3, 2, 1); }
        }
    }
}

static void s_bowl(G *g, const Cx *c, void *st) {
    (void)st;
    static const int P[6][2] = { {1, 0}, {3, 0}, {4, 0}, {6, 0}, {3, 1}, {4, 1} };
    int hit = c->f > .6;
    double u = (c->f - .6) / .4;
    for (int i = 0; i < 6; i++) {
        double px = P[i][0], py = P[i][1];
        if (!hit) set(g, px, py, 2, 1);
        else set(g, px + (px - 3.5 > 0 ? 1 : -1) * u * 4, py - u * 2 + (py ? u * 3 : 0), 2, 1 - u);
    }
    double y = jround(6 - 6 * dmin(1, c->f / .6));
    if (!hit) for (int q = 0; q < 4; q++) set(g, 3 + (q % 2), y + (q >> 1), c->step % 2, 1);
}

static void s_darts(G *g, const Cx *c, void *st) {
    (void)st;
    EACH { double d = cheb(x, y); set(g, x, y, d < 1 ? 2 : d < 2 ? 1 : 0, d < 1 ? .6 : .18); }
    long round = c->step / 4, k = c->step % 4;
    if (k == 3) return;
    for (long i = 0; i <= k; i++) {
        rng_t r = hash(round * 4 + i + 9);
        double tx = 1 + floor(rnext(&r) * 6), ty = 1 + floor(rnext(&r) * 6);
        if (i < k || c->f > .3) set(g, tx, ty, 2, 1);
        else { double u = c->f / .3; set(g, 7 + (tx - 7) * u, 7 + (ty - 7) * u, 2, 1); }
    }
}

static void s_seesaw(G *g, const Cx *c, void *st) {
    (void)st;
    double dir = c->step % 2 ? 1 : -1, tilt = -dir + 2 * dir * dmin(1, c->f * 4);
    set(g, 3, 7, 1, 1); set(g, 4, 7, 1, 1); set(g, 3.5, 6, 1, 1);
    line(g, 0, 5 - 2 * tilt, 7, 5 + 2 * tilt, 0, 1);
    double bx = 3.5 + dir * 3.2 * dmin(1, dmax(0, (c->f - .1) * 1.3)), by = 5 + 2 * tilt * (bx - 3.5) / 3.5 - 1;
    set(g, bx, by, 2, 1);
}

static void s_yoyo(G *g, const Cx *c, void *st) {
    (void)st;
    set(g, 3, 0, 2, 1); set(g, 4, 0, 2, 1);
    double y = jround(1 + 5 * sin(PI * c->f));
    for (int j = 1; j < y; j++) set(g, 3, j, 2, .3);
    for (int q = 0; q < 4; q++) set(g, 3 + (q % 2), y + (q >> 1), c->step % 3, 1);
}

static void s_coil(G *g, const Cx *c, void *st) {
    (void)st;
    long n = (long)floor(c->f * 65);
    for (int i = 0; i < 64; i++) { int x = g_spiral[i][0], y = g_spiral[i][1]; NEW_OLD(i < n); }
}

static void s_volley(G *g, const Cx *c, void *st) {
    (void)st;
    for (int y = 5; y < 8; y++) { set(g, 3, y, 2, .4); set(g, 4, y, 2, .4); }
    int right = c->step % 2 == 0;
    double x = right ? 1 + 5 * c->f : 6 - 5 * c->f, y = 6 - 5.5 * sin(PI * c->f);
    set(g, 1, 7, 0, right && c->f < .2 ? 1 : .4);
    set(g, 6, 7, 1, !right && c->f < .2 ? 1 : .4);
    set(g, x, y, 2, 1);
}

static void s_swing(G *g, const Cx *c, void *st) {
    (void)st;
    double a = .95 * cos(PI * SP), sx = 3.5 + 6 * sin(a), sy = 6 * cos(a);
    line(g, 3, 0, sx - .5, sy, 2, .35); line(g, 4, 0, sx + .5, sy, 2, .35);
    set(g, sx - .5, sy, 0, 1); set(g, sx + .5, sy, 0, 1);
}

typedef struct { int n; struct { double t, tx; } s[LIST_MAX]; } goal_t;
static void s_goal(G *g, const Cx *c, void *p) {
    goal_t *st = p;
    for (int x = 1; x <= 6; x++) set(g, x, 0, 2, .35);
    for (int y = 0; y < 3; y++) { set(g, 1, y, 2, .35); set(g, 6, y, 2, .35); }
    long kx = 2 + tri(c->step, 2);
    set(g, kx, 2, 1, 1); set(g, kx + 1, 2, 1, 1);
    if (c->kick && st->n < LIST_MAX) {
        st->s[st->n].t = c->time; st->s[st->n].tx = 2 + floor(hash1(floor(c->time * 10)) * 4); st->n++;
    }
    int m = 0;
    for (int i = 0; i < st->n; i++) if (c->time - st->s[i].t < .55) st->s[m++] = st->s[i];
    st->n = m;
    for (int i = 0; i < st->n; i++) {
        double u = (c->time - st->s[i].t) / .3;
        if (u < 1) set(g, 3.5 + (st->s[i].tx - 3.5) * u, 7 - 6 * u, 0, 1);
        else for (int x = 2; x <= 5; x++) for (int y = 0; y < 2; y++) set(g, x, y, 0, 1 - (u - 1) * .55);
    }
}

static double ramp(double x) { return 7 - jround(pow(fabs(x - 3.5) / 3.5, 2) * 5); }
static void s_skate(G *g, const Cx *c, void *st) {
    (void)st;
    for (int x = 0; x < 8; x++) set(g, x, ramp(x), 2, .35);
    double x = 3.5 - 3.5 * cos(PI * SP), rx = dmax(0, dmin(7, jround(x)));
    set(g, rx, ramp(rx) - 1, 0, 1); set(g, rx, ramp(rx) - 2, 1, 1);
}

static void s_mow(G *g, const Cx *c, void *st) {
    (void)st;
    long n = (long)floor(c->f * 65);
    EACH { int i = y * 8 + (y % 2 ? 7 - x : x); NEW_OLD(i < n); }
}

static void s_pour(G *g, const Cx *c, void *st) {
    (void)st;
    double L = c->f * 8.6;
    EACH { double s = L + .7 * sin(x * .9 + c->time * 6) * dmin(1, L) * (L < 8 ? 1 : 0); NEW_OLD(7 - y < s); }
}

static void s_ski(G *g, const Cx *c, void *st) {
    (void)st;
    double t = SP;
    long n = (long)floor(t * 2);
    for (int y = 0; y < 8; y++) {
        long Y = y + n;
        if (Y % 4) continue;
        int x = (Y / 4) % 2 ? 5 : 1;
        set(g, x, y, 1, 1); set(g, x + 1, y, 0, 1);
    }
    set(g, 3.5 - 2.4 * cos(PI * (t + .5) / 2), 1, 2, 1);
}

static void s_pool(G *g, const Cx *c, void *st) {
    (void)st;
    int m = c->step % 2;
#define X(v) (m ? 7 - (v) : (v))
    set(g, X(7), 0, 2, .5); set(g, X(0), 7, 2, .3); set(g, X(0), 0, 2, .3); set(g, X(7), 7, 2, .3);
    double u = dmin(1, c->f / .4), v = dmax(0, dmin(1, (c->f - .4) / .4));
    set(g, X(1 + 2 * u), 6 - 2 * u, 2, 1);
    if (v < 1) set(g, X(4 + 3 * v), 3 - 3 * v, 0, 1);
    else set(g, X(7), 0, 0, 1 - (c->f - .8) * 4);
#undef X
}

static void s_weave(G *g, const Cx *c, void *st) {
    (void)st;
    long n = (long)floor(c->f * 65);
    for (int i = 0; i < 64; i++) { int x = g_hilbert[i][0], y = g_hilbert[i][1]; NEW_OLD(i < n); }
}

/* ---- the table ------------------------------------------------------------ */

typedef struct {
    void (*init)(void *st);
    void (*draw)(G *g, const Cx *c, void *st);
} shape_t;

static const shape_t k_shapes[SHAPE_COUNT] = {
    { 0, s_pulse }, { 0, s_rings }, { 0, s_diamond }, { i_burst, s_burst }, { 0, s_sweep },
    { 0, s_cross }, { 0, s_snake }, { 0, s_radar }, { 0, s_stripes }, { 0, s_tunnel },
    { 0, s_quads }, { 0, s_shapes }, { 0, s_steps }, { 0, s_rise }, { 0, s_bounce },
    { 0, s_wave }, { i_rain, s_rain }, { 0, s_bars }, { 0, s_spiral }, { 0, s_pinwheel },
    { 0, s_kit }, { 0, s_icons }, { 0, s_count }, { 0, s_arrow }, { 0, s_corners },
    { i_stack, s_stack }, { 0, s_rocket }, { 0, s_halves }, { 0, s_pong }, { i_trace, s_trace },
    { 0, s_invaders }, { i_breakout, s_breakout }, { 0, s_text }, { 0, s_spin }, { 0, s_heart },
    { 0, s_sand }, { 0, s_chomp }, { 0, s_doors }, { i_flap, s_flap }, { i_jump, s_jump },
    { 0, s_noughts }, { 0, s_four }, { 0, s_road }, { 0, s_maze }, { 0, s_dice },
    { 0, s_catch }, { 0, s_lander }, { 0, s_tanks }, { 0, s_face }, { i_eye, s_eye },
    { 0, s_sun }, { 0, s_moon }, { 0, s_flower }, { 0, s_tick }, { i_speaker, s_speaker },
    { 0, s_saturn }, { 0, s_clock }, { 0, s_charge }, { 0, s_ufo }, { 0, s_wipe },
    { 0, s_curtain }, { 0, s_iris }, { 0, s_pie }, { 0, s_diag }, { 0, s_zoom },
    { 0, s_aim }, { 0, s_propeller }, { 0, s_eight }, { 0, s_helix }, { 0, s_metronome },
    { 0, s_cradle }, { 0, s_piston }, { 0, s_ping }, { 0, s_juggle }, { 0, s_frame },
    { 0, s_stairs }, { 0, s_swap }, { i_bubbles, s_bubbles }, { 0, s_train }, { 0, s_boxes },
    { 0, s_bass }, { 0, s_pads }, { 0, s_tally }, { 0, s_beats }, { 0, s_lift },
    { 0, s_hoop }, { 0, s_bowl }, { 0, s_darts }, { 0, s_seesaw }, { 0, s_yoyo },
    { 0, s_coil }, { 0, s_volley }, { 0, s_swing }, { 0, s_goal }, { 0, s_skate },
    { 0, s_mow }, { 0, s_pour }, { 0, s_ski }, { 0, s_pool }, { 0, s_weave },
};

_Static_assert(sizeof(burst_t) <= SHAPE_STATE_BYTES && sizeof(rain_t) <= SHAPE_STATE_BYTES &&
               sizeof(tower_t) <= SHAPE_STATE_BYTES && sizeof(shots_t) <= SHAPE_STATE_BYTES &&
               sizeof(tanks_t) <= SHAPE_STATE_BYTES && sizeof(bubbles_t) <= SHAPE_STATE_BYTES &&
               sizeof(goal_t) <= SHAPE_STATE_BYTES && sizeof(hoop_t) <= SHAPE_STATE_BYTES,
               "a scene's state outgrew SHAPE_STATE_BYTES");

void viz_shape_colour(int color, int ci, int x, int y, double out[3]) {
    G g = { NULL, color < 0 || color >= SHAPE_COLORS ? 0 : color, 0 };
    colour(&g, ci, x, y, out);
}

/* ---- the look: SHIFT, FOLD, REACT, TRAIL ----------------------------------- */

void viz_shapes_init(viz_shapes_t *s) {
    memset(s, 0, sizeof *s);
    s->scene = -1;
    tables();
}

void viz_shapes_frame(viz_shapes_t *s, int scene, const viz_shape_knobs_t *k, const viz_shape_feat_t *f) {
    tables();
    if (scene < 0 || scene >= SHAPE_COUNT) scene = 0;
    if (scene != s->scene) {
        s->scene = scene;
        memset(s->state, 0, sizeof s->state);
        if (k_shapes[scene].init) k_shapes[scene].init(s->state);
        memset(s->disp, 0, sizeof s->disp);
    }
    int react = k->react;
    double sp = f->beats * (k->steps_per_beat > 0 ? k->steps_per_beat : k_rates[k->rate < 0 || k->rate >= SHAPE_RATES ? 2 : k->rate]);
    long sh;
    switch (k->shift) {
    case 1:  sh = (long)floor(f->beats / (f->beats_per_bar > 0 ? f->beats_per_bar : 4)); break;
    case 2:  sh = (long)floor(f->beats); break;
    case 3:  sh = (long)f->kick_count; break;
    default: sh = 0; break;
    }
    if (sh < 0) sh = 0;
    int bump = react >= 2 && f->kick > .5 ? 1 : 0;
    int size = k->size + 1 + bump;

    Cx c;
    c.beats = f->beats; c.time = f->time; c.dt = f->dt;
    c.step = (long)floor(sp); c.f = sp - floor(sp);
    c.s = size < 4 ? size : 4;
    c.kick = f->kick_now; c.snare = f->snare_now; c.hat = f->hat_now;
    c.ft = f;

    G g = { s->cur, k->color, sh };
    memset(s->cur, 0, sizeof s->cur);
    k_shapes[scene].draw(&g, &c, s->state);

    /* FOLD: the top left is copied to the rest. */
    int m = k->mirror;
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++) {
            int sx = x, sy = y;
            if ((m == 1 || m >= 3) && sx > 3) sx = 7 - sx;
            if ((m == 2 || m >= 3) && sy > 3) sy = 7 - sy;
            if (m == 4 && sx > sy) { int t = sx; sx = sy; sy = t; }
            memcpy(&s->out[(y * 8 + x) * 3], &s->cur[(sy * 8 + sx) * 3], 3 * sizeof(float));
        }

    /* REACT: what the kick does to the whole picture. */
    if (react >= 1) {
        double gain = 1 + 0.7 * f->kick, base = react == 1 ? .55 + .45 * f->kick : 1;
        double mul = react == 1 ? base * 1.25 : gain;
        for (int i = 0; i < 192; i++) s->out[i] = (float)dmin(255, s->out[i] * mul);
    }
    if (react == 3) {
        double col[3], a = .45 * f->kick;
        colour(&g, 2, 4, 4, col);
        for (int i = 0; i < 64; i++)
            for (int j = 0; j < 3; j++) s->out[i * 3 + j] = (float)dmax(s->out[i * 3 + j], col[j] * a);
    }
    if (react == 4 && f->kick > .55) {
        double col[3];
        colour(&g, 1, 4, 4, col);
        for (int i = 0; i < 64; i++) {
            int lit = (double)s->out[i * 3] + s->out[i * 3 + 1] + s->out[i * 3 + 2] > 60;
            for (int j = 0; j < 3; j++) s->out[i * 3 + j] = lit ? 0 : (float)col[j];
        }
    }

    /* TRAIL: lights fade out instead of switching off. */
    double tau = k_trails[k->trail < 0 || k->trail >= SHAPE_TRAILS ? 1 : k->trail];
    double fade = tau ? exp(-f->dt / tau) : 0;
    for (int i = 0; i < 192; i++) s->disp[i] = (float)dmax(s->out[i], s->disp[i] * fade);
}
