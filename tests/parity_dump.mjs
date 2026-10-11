/*
 * parity_dump.mjs — run the scene gallery's own JavaScript and write down every
 * pad of every frame, for tests/test_parity.c to compare with the C port.
 *
 * The gallery (tools/gallery/gallery.html) is where scenes were chosen, so it
 * is the reference: the C must light the same pads in the same colours. This
 * lifts the gallery's script from "music" up to (not including) "page", which
 * holds everything but the DOM, and drives it exactly as the page does, minus
 * the drawing.
 *
 * Output: build/parity_js.bin, configs x scenes x frames x 192 bytes.
 * Run: node tests/parity_dump.mjs
 */
import { readFileSync, writeFileSync, mkdirSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { dirname, join } from "node:path";

const here = dirname(fileURLToPath(import.meta.url));
const html = readFileSync(join(here, "..", "tools", "gallery", "gallery.html"), "utf8");
const from = html.indexOf("/* ---------------- music");
const to = html.indexOf("/* ---------------- page");
if (from < 0 || to < 0) { console.error("FAIL: gallery markers not found"); process.exit(1); }
const chunk = html.slice(from, to);

/* Keep in step with test_parity.c. Knob indexes: color rate size trail mirror shift react. */
export const CONFIGS = [
  [2, 2, 1, 1, 0, 1, 1],   /* the gallery's defaults */
  [8, 4, 3, 4, 4, 3, 4],   /* Rainbow, 1/16, 4, Smear, Eight, Kick, Flip */
  [7, 0, 0, 0, 1, 2, 2],   /* Mono, 1 bar, 1, Off, Sides, Beat, Grow */
  [1, 3, 2, 3, 3, 0, 3],   /* Fire, 1/8, 3, Long, Four, Hold, Flash */
];
const FRAMES = 540, FPS = 30;

let out = null, names = null;
CONFIGS.forEach((cfg, ci) => {
  const api = new Function(chunk + "\nreturn { feat, listen, SCENES, Player, knob, KNOBS, BPM };")();
  const keys = ["color", "rate", "size", "trail", "mirror", "shift", "react"];
  keys.forEach((k, i) => (api.knob[k] = cfg[i]));
  const canvas = { getContext: () => null, width: 0 };
  const players = api.SCENES.map((_, i) => new api.Player(i, canvas));
  if (!out) {
    names = api.SCENES.map((s) => s.name);
    out = new Uint8Array(CONFIGS.length * players.length * FRAMES * 192);
  }
  const dt = 1 / FPS;
  for (let f = 0; f < FRAMES; f++) {
    const t = f / FPS, beats = t * api.BPM / 60;
    api.listen(beats, dt);
    players.forEach((p, si) => {
      p.frame(beats, dt, t, true);
      const base = ((ci * players.length + si) * FRAMES + f) * 192;
      for (let k = 0; k < 192; k++) out[base + k] = Math.max(0, Math.min(255, Math.round(p.disp[k])));
    });
  }
});
mkdirSync(join(here, "..", "build"), { recursive: true });
writeFileSync(join(here, "..", "build", "parity_js.bin"), out);
writeFileSync(join(here, "..", "build", "parity_names.txt"), names.join("\n") + "\n");
console.log(`parity: ${names.length} scenes x ${CONFIGS.length} configs x ${FRAMES} frames written`);
