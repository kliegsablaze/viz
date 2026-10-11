/*
 * make_canvas_data.mjs — fill the @generated block of src/canvas.js from the
 * scene gallery: every scene's name, each geometric scene's thumbnail (its
 * busiest frame of up to 40 lit pads in its first 8 s, with trail, fold, swap
 * and kick reaction off), each colour set's brightness, and the device's 4x5
 * label font.
 *
 * Run after changing a scene: node tools/gallery/make_canvas_data.mjs
 * tests/run.sh checks the block is up to date.
 */
import { readFileSync, writeFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { dirname, join } from "node:path";

const here = dirname(fileURLToPath(import.meta.url));
const root = join(here, "..", "..");
const html = readFileSync(join(here, "gallery.html"), "utf8");
const chunk = html.slice(html.indexOf("/* ---------------- music"), html.indexOf("/* ---------------- page"));
const api = new Function(chunk + "\nreturn { SCENES, Player, knob, listen, feat, PALETTES };")();

Object.assign(api.knob, { color: 7, rate: 2, size: 1, trail: 0, mirror: 0, shift: 0, react: 0 });
const cv = { getContext: () => null, width: 0 };
const ps = api.SCENES.map((_, i) => new api.Player(i, cv));
const best = api.SCENES.map(() => ({ score: -1, bits: null }));
for (let f = 0; f < 240; f++) {
    const t = f / 30, beats = t * 2;
    api.listen(beats, 1 / 30);
    ps.forEach((pl, i) => {
        pl.frame(beats, 1 / 30, t, true);
        const bits = new Uint8Array(64);
        let n = 0;
        for (let k = 0; k < 64; k++) if (pl.disp[k * 3] + pl.disp[k * 3 + 1] + pl.disp[k * 3 + 2] > 90) { bits[k] = 1; n++; }
        const score = n <= 40 ? n : 80 - n;
        if (score > best[i].score) best[i] = { score, bits };
    });
}
const hex = best.map(({ bits }) => { let s = ""; for (let k = 0; k < 64; k += 4) s += ((bits[k] << 3) | (bits[k + 1] << 2) | (bits[k + 2] << 1) | bits[k + 3]).toString(16); return s; });

const SOFT = ["Aurora", "Nebula", "Tide", "Fireflies", "Reeds", "Lava"];   /* src/dsp/scenes.c, after the 100 */
const names = api.SCENES.map((s) => s.name).concat(SOFT);

const font = JSON.parse(readFileSync(join(here, "font4x5.json"), "utf8"));
const glyphs = {};
for (const [ch, [w, px]] of Object.entries(font)) {
    const rows = [0, 0, 0, 0, 0];
    for (const [x, y] of px) rows[y] |= 1 << x;
    glyphs[ch] = [w, ...rows];
}

/* Each colour set's three colours, as bright as they look (0..1); Rainbow has none. */
const luma = {};
for (const [name, pal] of Object.entries(api.PALETTES)) if (pal) luma[name] = pal.map(([r, g, b]) => Math.round((0.3 * r + 0.59 * g + 0.11 * b) / 2.55) / 100);

const block = `/* @generated begin */
const SCENE_NAMES = ${JSON.stringify(names)};
const THUMBS = ${JSON.stringify(hex)};
const FONT = ${JSON.stringify(glyphs)};
const LUMA = ${JSON.stringify(luma)};
/* @generated end */`;
const path = join(root, "src", "canvas.js");
const src = readFileSync(path, "utf8");
const a = src.indexOf("/* @generated begin */"), b = src.indexOf("/* @generated end */") + "/* @generated end */".length;
const out = src.slice(0, a) + block + src.slice(b);
if (process.argv.includes("--check")) {
    if (out !== src) { console.error("FAIL: src/canvas.js's generated block is stale (node tools/gallery/make_canvas_data.mjs)"); process.exit(1); }
    console.log("ok: canvas.js data matches the gallery");
} else {
    writeFileSync(path, out);
    console.log(`wrote ${names.length} scene names, ${hex.length} thumbnails, ${Object.keys(glyphs).length} glyphs into src/canvas.js`);
}
