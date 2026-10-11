/*
 * score_scenes.mjs — measures every gallery scene for the user's scene review
 * (2026-10-11): how much of the grid is white, how much it flickers, and how
 * often it stands still. Runs the gallery's own engine (the C matches it pad
 * for pad), 16 s at 120 BPM and 30 fps, at Viz's Look defaults with REACT Off
 * so only the scene is measured.
 *
 *   node tools/gallery/score_scenes.mjs [--json out.json]
 *
 * white   share of the grid that is white-ish: bright (top channel over 120)
 *         and washed out (the lowest channel at least half the top one), which
 *         takes in pastels and two colours meeting on a pad (the engine keeps
 *         each channel's brightest, so green on magenta reads near white);
 *         averaged over the nine colour sets
 * flicker abrupt changes (a pad's brightness jumps by over 40% of full in one
 *         frame) per pad per second, with the gallery's drum loop
 * still   share of frames where nothing on the grid changes, in Beat mode (no
 *         sound), at RATE 1/4 and at RATE 1 bar
 */
import { readFileSync, writeFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { dirname, join } from "node:path";

const here = dirname(fileURLToPath(import.meta.url));
const html = readFileSync(join(here, "gallery.html"), "utf8");
const chunk = html.slice(html.indexOf("/* ---------------- music"), html.indexOf("/* ---------------- page"));
const api = new Function(chunk + "\nreturn { SCENES, Player, knob, listen, feat, PALETTES, KNOBS };")();
const { SCENES, Player, knob, listen, feat, KNOBS } = api;
const DEF = { color: 2, rate: 2, size: 1, trail: 1, mirror: 0, shift: 1, react: 0 };
const cv = { getContext: () => null, width: 0 };
const FPS = 30, SECS = 16;

function resetFeat() {
  Object.assign(feat, { kick: 0, snare: 0, hat: 0, bass: 0, kickCount: 0, last16: -1, kickNow: false, snareNow: false, hatNow: false });
  feat.bands.fill(0);
}
/* Runs every scene together; calls fn(i, disp, prev) each frame. */
function run(knobs, sound, fn) {
  Object.assign(knob, DEF, knobs);
  resetFeat();
  const ps = SCENES.map((_, i) => new Player(i, cv));
  const prev = SCENES.map(() => new Float32Array(192));
  for (let f = 0; f < FPS * SECS; f++) {
    const t = f / FPS, beats = t * 2;
    if (sound) listen(beats, 1 / FPS);
    ps.forEach((pl, i) => { pl.frame(beats, 1 / FPS, t, true); if (f > 0) fn(i, pl.disp, prev[i]); prev[i].set(pl.disp); });
  }
}
const lum = (d, k) => (0.3 * d[k] + 0.59 * d[k + 1] + 0.11 * d[k + 2]) / 255;
const n = SCENES.length, frames = FPS * SECS - 1;

const white = new Float64Array(n);
const palettes = KNOBS[1].opts;
palettes.forEach((_, ci) => run({ color: ci }, true, (i, d) => {
  let w = 0;
  for (let k = 0; k < 192; k += 3) {
    const mx = Math.max(d[k], d[k + 1], d[k + 2]), mn = Math.min(d[k], d[k + 1], d[k + 2]);
    if (mx > 120 && mn >= 0.5 * mx) w++;
  }
  white[i] += w / 64 / frames / palettes.length;
}));

const flicker = new Float64Array(n), lit = new Float64Array(n);
run({}, true, (i, d, p) => {
  let e = 0, l = 0;
  for (let k = 0; k < 192; k += 3) { if (Math.abs(lum(d, k) - lum(p, k)) > 0.4) e++; if (lum(d, k) > 0.05) l++; }
  flicker[i] += e / 64 / SECS;
  lit[i] += l / 64 / frames;
});

function stillAt(rate) {
  const s = new Float64Array(n);
  run({ rate }, false, (i, d, p) => {
    let ch = 0;
    for (let k = 0; k < 192; k++) ch += Math.abs(d[k] - p[k]);
    if (ch < 3) s[i] += 1 / frames;
  });
  return s;
}
const still4 = stillAt(2), still1 = stillAt(0);

const rows = SCENES.map((s, i) => ({ name: s.name, white: +white[i].toFixed(3), flicker: +flicker[i].toFixed(2),
  still: +still4[i].toFixed(2), stillBar: +still1[i].toFixed(2), lit: +lit[i].toFixed(2) }));
/* --review: rewrite the gallery's REVIEW table from these scores. */
const SOUND_ONLY = ["Burst", "Rain", "Bars", "Kit", "Bass", "Pads", "Tally"];   /* seq.c viz_seq_sound_only */
const LIMIT = { white: 0.08, flicker: 1.0, still: 0.45 };
if (process.argv.includes("--review")) {
  const rev = {};
  for (const r of rows) {
    const why = [];
    if (SOUND_ONLY.includes(r.name)) why.push("sound");
    if (r.white >= LIMIT.white) why.push("white");
    if (r.flicker >= LIMIT.flicker) why.push("flicker");
    if (r.still >= LIMIT.still && !SOUND_ONLY.includes(r.name)) why.push("still");
    if (why.length) rev[r.name] = { why, white: r.white, flicker: r.flicker, still: r.still };
  }
  const g = join(here, "gallery.html"), src = readFileSync(g, "utf8");
  writeFileSync(g, src.replace(/const REVIEW = .*;\n/, `const REVIEW = ${JSON.stringify(rev)};\n`));
  console.log(`review: ${Object.keys(rev).length} scenes flagged`);
}
const out = process.argv.indexOf("--json");
if (out > 0) writeFileSync(process.argv[out + 1], JSON.stringify(rows, null, 1));
const show = (key, k) => console.log(`\nmost ${key}:\n` + [...rows].sort((a, b) => b[key] - a[key]).slice(0, k)
  .map((r) => `  ${r.name.padEnd(10)} white ${r.white.toFixed(2)}  flicker ${r.flicker.toFixed(2)}  still ${r.still.toFixed(2)} / bar ${r.stillBar.toFixed(2)}  lit ${r.lit.toFixed(2)}`).join("\n"));
show("white", 20); show("flicker", 25); show("still", 25); show("stillBar", 15);
