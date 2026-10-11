/*
 * preview — render Viz's scenes on a Mac into one self-contained HTML page
 * (DESIGN.md, "Preview tool").
 *
 *   build/preview [--wav file.wav] [--bpm 120] [--play S-E] [--seconds N]
 *                 [--scene Name] [--color Name] [--mode Auto] [--out page.html]
 *
 * With no --wav it plays a built-in demo: a 120 BPM groove with a breakdown,
 * Move "playing" for 28 s, then stopped over an ambient pad, then silence.
 * The same listening and rendering code as the module runs here, block by
 * block; only the clock is scripted. The page plays the audio and shows the
 * grid as the Matrix would, underglow included.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <zlib.h>

#include "viz.h"

#define SR VIZ_SR

/* ---- the demo track ----------------------------------------------------- */

static float frand(uint32_t *s) { return viz_randf(s) * 2 - 1; }

static int16_t *demo(int seconds, float bpm, double *play_end) {
    long n = (long)seconds * SR;
    float *mix = calloc((size_t)n, sizeof(float));
    int16_t *out = calloc((size_t)n * 2, sizeof(int16_t));
    uint32_t rng = 7;
    double beat_s = 60.0 / bpm;
    double stop = seconds - 4.0 - 4.0;          /* the clock stops 8 s before the end */
    *play_end = stop;
    /* pad: A minor, slow swell, all the way through */
    const float chord[4] = { 220.0f, 261.63f, 329.63f, 392.0f };
    for (long i = 0; i < n; i++) {
        double t = (double)i / SR;
        float env = (float)fmin(1.0, t / 2.0) * (t > seconds - 4.0 ? (float)fmax(0, (seconds - 1.0 - t) / 3.0) : 1.0f);
        float v = 0;
        for (int k = 0; k < 4; k++)
            v += sinf((float)(2 * M_PI * chord[k] * t)) * 0.5f + sinf((float)(2 * M_PI * chord[k] * 1.003 * t)) * 0.5f;
        mix[i] += v * 0.035f * env * (0.8f + 0.2f * sinf((float)(t * 0.7)));
    }
    long beats = (long)(stop / beat_s);
    for (long b = 0; b < beats; b++) {
        double t0 = b * beat_s;
        int bar = (int)(b / 4), pos = (int)(b % 4);
        int breakdown = bar >= 4 && bar < 6;       /* bars 5-6: pad and hats only */
        int intro = bar < 1;
        long i0 = (long)(t0 * SR);
        if (!breakdown && !intro) {
            /* kick */
            for (long i = 0; i < (long)(0.3 * SR) && i0 + i < n; i++) {
                double t = (double)i / SR;
                double f = 45 + 80 * exp(-t * 30);
                static double ph; if (i == 0) ph = 0;
                ph += 2 * M_PI * f / SR;
                mix[i0 + i] += (float)(sin(ph) * exp(-t * 9) * 0.55);
            }
            /* snare on 2 and 4 */
            if (pos == 1 || pos == 3)
                for (long i = 0; i < (long)(0.18 * SR) && i0 + i < n; i++) {
                    double t = (double)i / SR;
                    mix[i0 + i] += (float)((frand(&rng) * 0.6 + sin(2 * M_PI * 185 * t) * 0.4) * exp(-t * 22) * 0.35);
                }
            /* bass on the off 8ths */
            long ib = (long)((t0 + beat_s / 2) * SR);
            float bf = (bar % 2) ? 55.0f : 65.4f;
            for (long i = 0; i < (long)(beat_s / 2 * SR) && ib + i < n; i++) {
                double t = (double)i / SR;
                double saw = fmod(bf * t, 1.0) * 2 - 1;
                mix[ib + i] += (float)(saw * exp(-t * 6) * 0.18);
            }
        }
        /* hats on 8ths (and 16ths in the second half) */
        int sub = bar >= 6 ? 4 : 2;
        for (int h = 0; h < sub; h++) {
            long ih = (long)((t0 + beat_s * h / sub) * SR);
            float prev = 0;
            for (long i = 0; i < (long)(0.04 * SR) && ih + i < n; i++) {
                double t = (double)i / SR;
                float x = frand(&rng);
                float hp = x - prev; prev = x;
                mix[ih + i] += (float)(hp * exp(-t * 90) * (h % 2 ? 0.07 : 0.12));
            }
        }
    }
    for (long i = 0; i < n; i++) {
        float v = tanhf(mix[i] * 1.2f) * 0.8f;
        out[2 * i] = out[2 * i + 1] = (int16_t)(v * 32767);
    }
    free(mix);
    return out;
}

/* ---- WAV in -------------------------------------------------------------- */

static int16_t *read_wav(const char *path, long *frames) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    unsigned char h[12];
    if (fread(h, 1, 12, f) != 12 || memcmp(h, "RIFF", 4) || memcmp(h + 8, "WAVE", 4)) { fclose(f); return NULL; }
    int ch = 0, bits = 0, rate = 0;
    int16_t *out = NULL;
    for (;;) {
        unsigned char c[8];
        if (fread(c, 1, 8, f) != 8) break;
        unsigned len = c[4] | c[5] << 8 | c[6] << 16 | (unsigned)c[7] << 24;
        if (!memcmp(c, "fmt ", 4)) {
            unsigned char fmt[64];
            unsigned k = len < sizeof fmt ? len : sizeof fmt;
            if (fread(fmt, 1, k, f) != k) break;
            if (len > k) fseek(f, len - k, SEEK_CUR);
            ch = fmt[2] | fmt[3] << 8;
            rate = fmt[4] | fmt[5] << 8 | fmt[6] << 16 | fmt[7] << 24;
            bits = fmt[14] | fmt[15] << 8;
        } else if (!memcmp(c, "data", 4)) {
            if (bits != 16 || (ch != 1 && ch != 2) || rate != SR) {
                fprintf(stderr, "preview: need 16-bit 44.1 kHz mono or stereo (got %d-bit %d Hz %d ch)\n", bits, rate, ch);
                break;
            }
            long n = len / (2 * ch);
            int16_t *raw = malloc(len);
            if (fread(raw, 1, len, f) != len) { free(raw); break; }
            out = malloc((size_t)n * 4);
            for (long i = 0; i < n; i++) {
                out[2 * i] = raw[i * ch];
                out[2 * i + 1] = raw[i * ch + (ch - 1)];
            }
            free(raw);
            *frames = n;
            break;
        } else {
            fseek(f, len + (len & 1), SEEK_CUR);
        }
    }
    fclose(f);
    return out;
}

/* ---- base64 ---------------------------------------------------------------- */

static void b64(FILE *o, const unsigned char *d, size_t n) {
    static const char t[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    for (size_t i = 0; i < n; i += 3) {
        unsigned v = d[i] << 16 | (i + 1 < n ? d[i + 1] << 8 : 0) | (i + 2 < n ? d[i + 2] : 0);
        fputc(t[v >> 18 & 63], o);
        fputc(t[v >> 12 & 63], o);
        fputc(i + 1 < n ? t[v >> 6 & 63] : '=', o);
        fputc(i + 2 < n ? t[v & 63] : '=', o);
    }
}

static void b64_deflate(FILE *o, const unsigned char *d, size_t n) {
    uLongf cap = compressBound((uLong)n);
    unsigned char *z = malloc(cap);
    compress2(z, &cap, d, (uLong)n, 9);
    b64(o, z, cap);
    free(z);
}

/* The demo's audio, as a small mono WAV for the page to play. */
static void b64_wav(FILE *o, const int16_t *st, long frames) {
    long n = frames / 2;                         /* 22.05 kHz */
    size_t bytes = 44 + (size_t)n * 2;
    unsigned char *w = malloc(bytes);
    unsigned rate = SR / 2;
    memcpy(w, "RIFF", 4);
    unsigned v = (unsigned)bytes - 8; memcpy(w + 4, &v, 4);
    memcpy(w + 8, "WAVEfmt ", 8);
    v = 16; memcpy(w + 16, &v, 4);
    uint16_t s16 = 1; memcpy(w + 20, &s16, 2);
    memcpy(w + 22, &s16, 2);
    memcpy(w + 24, &rate, 4);
    v = rate * 2; memcpy(w + 28, &v, 4);
    s16 = 2; memcpy(w + 32, &s16, 2);
    s16 = 16; memcpy(w + 34, &s16, 2);
    memcpy(w + 36, "data", 4);
    v = (unsigned)n * 2; memcpy(w + 40, &v, 4);
    for (long i = 0; i < n; i++) {
        int a = (st[4 * i] + st[4 * i + 1] + st[4 * i + 2] + st[4 * i + 3]) / 4;
        int16_t x = (int16_t)a;
        memcpy(w + 44 + i * 2, &x, 2);
    }
    b64(o, w, bytes);
    free(w);
}

/* ---- main ---------------------------------------------------------------- */

static int find(const char *const *names, int n, const char *v) {
    for (int i = 0; i < n; i++) if (!strcasecmp(names[i], v)) return i;
    return -1;
}

/* A colour each scene is shown in by default, chosen to suit it. */
static const int k_scene_color[SCENE_COUNT] = { 5, 0, 1, 3, 3, 2, 7, 4, 9 };

int main(int argc, char **argv) {
    const char *wav = NULL, *out = "build/preview.html";
    float bpm = 120;
    int seconds = 36, only = -1, color = -1, mode = MODE_AUTO, stats = 0;
    double play_from = 0, play_to = -1;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : "";
        if (!strcmp(a, "--wav")) { wav = v; i++; }
        else if (!strcmp(a, "--stats")) stats = 1;
        else if (!strcmp(a, "--out")) { out = v; i++; }
        else if (!strcmp(a, "--bpm")) { bpm = (float)atof(v); i++; }
        else if (!strcmp(a, "--seconds")) { seconds = atoi(v); i++; }
        else if (!strcmp(a, "--play")) { sscanf(v, "%lf-%lf", &play_from, &play_to); i++; }
        else if (!strcmp(a, "--scene")) { only = find(viz_scene_names, SCENE_COUNT, v); i++; }
        else if (!strcmp(a, "--color")) { color = find(viz_color_names, COLOR_COUNT, v); i++; }
        else if (!strcmp(a, "--mode")) {
            static const char *const m[] = { "Auto", "Beat", "Sound", "Blend" };
            mode = find(m, 4, v); if (mode < 0) mode = 0; i++;
        } else { fprintf(stderr, "preview: unknown option %s\n", a); return 2; }
    }

    long frames;
    int16_t *audio;
    if (wav) {
        audio = read_wav(wav, &frames);
        if (!audio) { fprintf(stderr, "preview: cannot read %s\n", wav); return 1; }
        if (frames > (long)seconds * SR) frames = (long)seconds * SR;
        if (play_to < 0) play_to = 0;           /* a file has no clock unless asked */
    } else {
        double end;
        audio = demo(seconds, bpm, &end);
        frames = (long)seconds * SR;
        if (play_to < 0) { play_from = 0; play_to = end; }
    }

    /* Listen once; every scene renders from the same features. */
    int per_frame = SR / VIZ_FPS;
    int nf = (int)(frames / per_frame);
    viz_features_t *feats = calloc((size_t)nf, sizeof *feats);
    viz_listen_t L;
    viz_listen_init(&L);
    viz_features_t f;
    int fi = 0, acc = 0;
    for (long pos = 0; pos + VIZ_BLOCK <= frames && fi < nf; pos += VIZ_BLOCK) {
        double t = (double)pos / SR;
        double beat = t >= play_from && t < play_to ? (t - play_from) * bpm / 60.0 : -1;
        viz_listen_block(&L, audio + 2 * pos, VIZ_BLOCK, beat, bpm, 4, mode, &f);
        acc += VIZ_BLOCK;
        while (acc >= per_frame && fi < nf) { acc -= per_frame; feats[fi++] = f; }
    }
    nf = fi;

    int scenes[SCENE_COUNT], ns = 0;
    for (int s = 0; s < SCENE_COUNT; s++) if (only < 0 || only == s) scenes[ns++] = s;
    unsigned char *pix = malloc((size_t)nf * NLIGHT * 3);

    FILE *o = fopen(out, "w");
    if (!o) { fprintf(stderr, "preview: cannot write %s\n", out); return 1; }
    fprintf(o, "<!doctype html><html><head><meta charset=utf-8><meta name=viewport content=\"width=device-width,initial-scale=1\">"
               "<title>Viz preview</title>\n<style>\n");
    fprintf(o, ":root{--bg:#0d0d10;--fg:#e8e6e3;--mut:#8a8790;--card:#18181d;--acc:#c9a3ff}\n"
               "body{margin:0;background:var(--bg);color:var(--fg);font:15px/1.4 -apple-system,system-ui,sans-serif}\n"
               "main{max-width:720px;margin:0 auto;padding:20px 16px 40px}\n"
               "h1{font-size:20px;margin:0 0 4px}p{color:var(--mut);margin:0 0 14px}\n"
               "#stage{background:#050507;border-radius:14px;padding:18px;display:flex;justify-content:center}\n"
               "canvas{width:100%%;max-width:440px;aspect-ratio:1;display:block}\n"
               ".row{display:flex;flex-wrap:wrap;gap:8px;margin:14px 0}\n"
               "button{background:var(--card);color:var(--fg);border:1px solid #2c2b33;border-radius:999px;padding:8px 14px;font:inherit;cursor:pointer}\n"
               "button.on{border-color:var(--acc);color:var(--acc)}\n"
               "#bar{width:100%%;accent-color:var(--acc)}#info{color:var(--mut);font-size:13px;display:flex;justify-content:space-between}\n"
               "</style></head><body><main>\n<h1>Viz preview</h1>\n"
               "<p>The Matrix as Viz would light it: 8x8 pads and the underglow, 30 frames a second. "
               "Press play to hear the music it reacts to.</p>\n"
               "<div id=stage><canvas id=c width=880 height=880></canvas></div>\n"
               "<div class=row><button id=play>Play</button><input id=bar type=range min=0 max=1000 value=0></div>\n"
               "<div id=info><span id=when></span><span id=sec></span></div>\n<div class=row id=scenes></div>\n<script>\n");
    fprintf(o, "const FPS=%d,PLAY=[%.3f,%.3f];\nconst SCENES=[\n", VIZ_FPS, play_from, play_to);
    for (int k = 0; k < ns; k++) {
        int sc = scenes[k];
        viz_look_t look = { .scene = sc, .color = color >= 0 ? color : k_scene_color[sc], .flow = 0.5f, .grow = 0.5f,
                            .bloom = 0.4f, .react = 0.6f, .warp = 0.4f, .drift = 0.3f, .light = 0.8f,
                            .edge = EDGEMODE_GLOW, .edge_weight = 2.0f, .edge_sides = 15 };
        viz_render_t *r = viz_render_new(0x5EED1234u);
        viz_frame_t fr;
        memset(&fr, 0, sizeof fr);
        for (int i = 0; i < nf; i++) {
            viz_render_frame(r, &look, &feats[i], 1.0f / VIZ_FPS, &fr);
            memcpy(pix + (size_t)i * NLIGHT * 3, fr.rgb, NLIGHT * 3);
        }
        viz_render_free(r);
        fprintf(o, "{name:\"%s\",color:\"%s\",d:\"", viz_scene_names[sc], viz_color_names[look.color]);
        b64_deflate(o, pix, (size_t)nf * NLIGHT * 3);
        fprintf(o, "\"},\n");
        fprintf(stderr, "preview: %s (%s), %d frames\n", viz_scene_names[sc], viz_color_names[look.color], nf);
        if (stats) {
            /* pad brightness (mean of R,G,B, 0..255): average, how many pads are
             * near black, how many bright, and how much the picture changes */
            double sum = 0, change = 0; long dark = 0, bright = 0, cnt = 0;
            for (int i = 0; i < nf; i++)
                for (int p = 0; p < NPAD; p++) {
                    const unsigned char *c = pix + ((size_t)i * NLIGHT + p) * 3;
                    double b = (c[0] + c[1] + c[2]) / 3.0;
                    sum += b; cnt++;
                    if (b < 12) dark++;
                    if (b > 120) bright++;
                    if (i) { const unsigned char *q = c - NLIGHT * 3; change += abs(c[0] - q[0]) + abs(c[1] - q[1]) + abs(c[2] - q[2]); }
                }
            fprintf(stderr, "  mean %5.1f  dark %4.1f%%  bright %4.1f%%  change/frame %5.1f\n",
                    sum / cnt, 100.0 * dark / cnt, 100.0 * bright / cnt, change / (nf - 1) / NPAD);
        }
    }
    fprintf(o, "];\nconst AUDIO=\"data:audio/wav;base64,");
    b64_wav(o, audio, (long)nf * per_frame);
    fprintf(o, "\";\n");
    fprintf(o, "%s",
        "const N=" "0;\n"
        "async function inflate(b){const bin=Uint8Array.from(atob(b),c=>c.charCodeAt(0));"
        "const s=new Blob([bin]).stream().pipeThrough(new DecompressionStream('deflate'));"
        "return new Uint8Array(await new Response(s).arrayBuffer());}\n"
        "const cv=document.getElementById('c'),g=cv.getContext('2d'),au=new Audio(AUDIO);\n"
        "let cur=0,data=[],frames=0;\n"
        "function pad(i){const x=i%8,y=(i/8)|0;return [x,y];}\n"
        "function draw(t){const d=data[cur];if(!d)return;let f=Math.min(frames-1,Math.max(0,Math.floor(t*FPS)));\n"
        " const o=f*96*3,W=cv.width,M=110,S=(W-2*M)/8;g.fillStyle='#050507';g.fillRect(0,0,W,W);\n"
        " g.save();g.filter='blur(26px)';\n"
        " for(let s=0;s<4;s++)for(let i=0;i<8;i++){const k=o+(64+s*8+i)*3,c=`rgb(${d[k]},${d[k+1]},${d[k+2]})`;g.fillStyle=c;\n"
        "  let x,y,w,h;if(s==0){x=W-M+8;y=M+i*S;w=40;h=S}else if(s==1){x=M-48;y=M+i*S;w=40;h=S}else if(s==2){x=M+i*S;y=W-M+8;w=S;h=40}else{x=M+i*S;y=M-48;w=S;h=40}g.fillRect(x,y,w,h);}\n"
        " g.restore();g.fillStyle='#141418';g.beginPath();g.roundRect(M-14,M-14,W-2*M+28,W-2*M+28,22);g.fill();\n"
        " for(let i=0;i<64;i++){const [x,y]=pad(i),k=o+i*3,r=d[k],gg=d[k+1],b=d[k+2];\n"
        "  const px=M+x*S+5,py=M+y*S+5,sz=S-10;g.fillStyle=`rgb(${r},${gg},${b})`;g.shadowColor=g.fillStyle;g.shadowBlur=(r+gg+b)/30;\n"
        "  g.beginPath();g.roundRect(px,py,sz,sz,9);g.fill();g.shadowBlur=0;\n"
        "  g.fillStyle='rgba(255,255,255,.035)';g.beginPath();g.roundRect(px,py,sz,sz,9);g.fill();}\n"
        " const play=t>=PLAY[0]&&t<PLAY[1];document.getElementById('when').textContent=play?'Move playing (clock)':'Move stopped (sound only)';\n"
        " document.getElementById('sec').textContent=t.toFixed(1)+' s';}\n"
        "function loop(){draw(au.currentTime);bar.value=au.duration?au.currentTime/au.duration*1000:0;if(!au.paused)requestAnimationFrame(loop);}\n"
        "const bar=document.getElementById('bar'),pb=document.getElementById('play');\n"
        "pb.onclick=()=>{if(au.paused){au.play();pb.textContent='Pause';loop();}else{au.pause();pb.textContent='Play';}};\n"
        "au.onended=()=>{pb.textContent='Play';};\n"
        "bar.oninput=()=>{if(au.duration){au.currentTime=bar.value/1000*au.duration;draw(au.currentTime);}};\n"
        "const box=document.getElementById('scenes');\n"
        "SCENES.forEach((s,i)=>{const b=document.createElement('button');b.textContent=s.name+' \\u00b7 '+s.color;b.onclick=()=>{cur=i;[...box.children].forEach((c,j)=>c.className=j==i?'on':'');draw(au.currentTime);};box.appendChild(b);});\n"
        "(async()=>{for(const s of SCENES)data.push(await inflate(s.d));frames=data[0].length/288;box.children[0].className='on';draw(0);})();\n"
        "</script></main></body></html>\n");
    fclose(o);
    fprintf(stderr, "preview: wrote %s (%d scenes, %.1f s)\n", out, ns, (double)nf / VIZ_FPS);
    free(pix); free(feats); free(audio);
    return 0;
}
