/*
 * test_parity.c — the C scenes against the gallery they were chosen in.
 *
 * tests/parity_dump.mjs runs tools/gallery/gallery.html's JavaScript and writes
 * every pad of every frame; this runs shapes.c on the same drum loop, the same
 * knobs and the same clock, and compares. The loop and the listener below are
 * the gallery's hitsAt() and listen(), ported, so both sides hear the same
 * music.
 *
 * A pad may differ by one level (the last bit of a float), nothing more. The
 * report names the scene, the knobs and the first frame that went wrong.
 *
 * Run: tests/run.sh (it dumps first).
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/dsp/shapes.h"

#define FRAMES 540
#define FPS 30
#define BPM 120
#define NCFG 4

static const int k_cfg[NCFG][7] = {   /* keep in step with parity_dump.mjs */
    { 2, 2, 1, 1, 0, 1, 1 },
    { 8, 4, 3, 4, 4, 3, 4 },
    { 7, 0, 0, 0, 1, 2, 2 },
    { 1, 3, 2, 3, 3, 0, 3 },
};

/* ---- the gallery's music and listener ------------------------------------ */

static int hits_at(long n16) {
    long bar = (n16 / 16) % 8, s = n16 % 16;
    int brk = bar >= 6;
    int kick = !brk && (s % 4 == 0 || (bar % 4 == 3 && s == 14));
    int snare = !brk && (s == 4 || s == 12 || (bar % 4 == 3 && s >= 13));
    int hat = brk ? s % 4 == 2 : (s % 2 == 0 || s % 4 == 3);
    int bass = !brk && (s == 0 || s == 3 || s == 6 || s == 10);
    return (kick ? HIT_KICK : 0) | (snare ? HIT_SNARE : 0) | (hat ? HIT_HAT : 0) | (bass ? 8 : 0);
}

typedef struct { viz_shape_feat_t f; long last16; } listener_t;

static void listen(listener_t *L, double beats, double dt) {
    viz_shape_feat_t *f = &L->f;
    long n16 = (long)floor(beats * 4);
    f->kick_now = f->snare_now = f->hat_now = 0;
    if (n16 != L->last16) {
        for (long k = L->last16 < 0 ? n16 : L->last16 + 1; k <= n16; k++) {
            int h = hits_at(k);
            if (h & HIT_KICK) { f->kick = 1; f->kick_now = 1; f->kick_count++; }
            if (h & HIT_SNARE) { f->snare = 1; f->snare_now = 1; }
            if (h & HIT_HAT) { f->hat = 1; f->hat_now = 1; }
            if (h & 8) f->bass = 1;
            f->log[k % 64] = (uint8_t)(h & 7);
        }
        L->last16 = n16;
    }
    f->kick *= exp(-dt / 0.14); f->snare *= exp(-dt / 0.12);
    f->hat *= exp(-dt / 0.04); f->bass *= exp(-dt / 0.18);
    static const double w[4][8] = { {1, .7, .2, 0, 0, 0, 0, 0}, {.3, .9, .7, .2, 0, 0, 0, 0},
                                    {0, 0, .2, .5, .9, .7, .3, 0}, {0, 0, 0, 0, .1, .4, .8, 1} };
    double src[4] = { f->kick, f->bass, f->snare, f->hat };
    for (int b = 0; b < 8; b++) {
        double v = 0.08 + 0.05 * sin(beats * 1.7 + b);
        for (int i = 0; i < 4; i++) v += w[i][b] * src[i];
        double target = v < 1 ? v : 1, cur = f->bands[b];
        f->bands[b] = (float)(cur + (target - cur) * (target > cur ? 0.6 : 0.12));
    }
}

int main(int argc, char **argv) {
    const char *path = argc > 1 ? argv[1] : "build/parity_js.bin";
    const char *npath = argc > 2 ? argv[2] : "build/parity_names.txt";
    FILE *fp = fopen(path, "rb");
    if (!fp) { fprintf(stderr, "FAIL: %s missing (run node tests/parity_dump.mjs)\n", path); return 1; }
    size_t per = (size_t)SHAPE_COUNT * FRAMES * 192, total = per * NCFG;
    uint8_t *js = malloc(total);
    if (!js || fread(js, 1, total, fp) != total) { fprintf(stderr, "FAIL: %s is the wrong size\n", path); return 1; }
    fclose(fp);

    /* the gallery's scene order must be ours */
    int failures = 0;
    FILE *np = fopen(npath, "r");
    if (!np) { fprintf(stderr, "FAIL: %s missing\n", npath); return 1; }
    char line[64];
    for (int i = 0; i < SHAPE_COUNT; i++) {
        if (!fgets(line, sizeof line, np)) { fprintf(stderr, "FAIL: gallery has %d scenes, C has %d\n", i, SHAPE_COUNT); return 1; }
        line[strcspn(line, "\n")] = 0;
        if (strcmp(line, viz_shape_names[i])) {
            fprintf(stderr, "FAIL: scene %d is %s in the gallery, %s in C\n", i + 1, line, viz_shape_names[i]);
            failures++;
        }
    }
    if (fgets(line, sizeof line, np)) { fprintf(stderr, "FAIL: gallery has more than %d scenes\n", SHAPE_COUNT); failures++; }
    fclose(np);
    if (failures) return 1;

    static viz_shapes_t sh[SHAPE_COUNT];
    int bad_scene[SHAPE_COUNT] = { 0 };
    long off_by_one = 0, compared = 0;
    for (int c = 0; c < NCFG; c++) {
        viz_shape_knobs_t k = { k_cfg[c][0], k_cfg[c][1], k_cfg[c][2], k_cfg[c][3], k_cfg[c][4], k_cfg[c][5], k_cfg[c][6] };
        listener_t L;
        memset(&L, 0, sizeof L);
        L.last16 = -1;
        L.f.beats_per_bar = 4;
        for (int s = 0; s < SHAPE_COUNT; s++) viz_shapes_init(&sh[s]);
        int reported[SHAPE_COUNT] = { 0 };
        double dt = 1.0 / FPS;
        for (int fr = 0; fr < FRAMES; fr++) {
            double t = (double)fr / FPS, beats = t * BPM / 60;
            listen(&L, beats, dt);
            L.f.beats = beats; L.f.time = t; L.f.dt = dt;
            for (int s = 0; s < SHAPE_COUNT; s++) {
                viz_shapes_frame(&sh[s], s, &k, &L.f);
                const uint8_t *ref = js + c * per + ((size_t)s * FRAMES + fr) * 192;
                for (int i = 0; i < 192; i++) {
                    double v = floor(sh[s].disp[i] + 0.5);
                    int mine = v < 0 ? 0 : v > 255 ? 255 : (int)v, d = abs(mine - ref[i]);
                    compared++;
                    if (d == 1) off_by_one++;
                    if (d > 1 && !reported[s] && getenv("PARITY_DEBUG")) {
                        fprintf(stderr, "%s cfg %d frame %d beats %.17g\n  C       / gallery (red channel)\n", viz_shape_names[s], c, fr, beats);
                        for (int y = 0; y < 8; y++) {
                            for (int x = 0; x < 8; x++) fprintf(stderr, "%4d", (int)floor(sh[s].disp[(y * 8 + x) * 3] + .5));
                            fprintf(stderr, "   |");
                            for (int x = 0; x < 8; x++) fprintf(stderr, "%4d", ref[(y * 8 + x) * 3]);
                            fprintf(stderr, "\n");
                        }
                    }
                    if (d > 1 && !reported[s]) {
                        reported[s] = 1;
                        bad_scene[s]++;
                        fprintf(stderr, "FAIL: %-10s config %d frame %d pad %d,%d channel %d: C %d, gallery %d\n",
                                viz_shape_names[s], c, fr, (i / 3) % 8, (i / 3) / 8, i % 3, mine, ref[i]);
                    }
                }
            }
        }
    }
    int nbad = 0;
    for (int s = 0; s < SHAPE_COUNT; s++) nbad += bad_scene[s] > 0;
    printf("parity: %d scenes x %d configs x %d frames, %ld values, %ld off by one level\n",
           SHAPE_COUNT, NCFG, FRAMES, compared, off_by_one);
    if (nbad) { fprintf(stderr, "SUITE FAILED: %d scene(s) differ from the gallery\n", nbad); return 1; }
    printf("PASS: every scene matches the gallery\n");
    free(js);
    return 0;
}
