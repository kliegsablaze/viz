// Draws Viz's knob pictures (src/canvas.js) through the host's own widget
// registry, frame context and viz resolver, on the Look, Sequence (every step)
// and Mod pages. Usage: node widgets.test.mjs <dump dir> <schwung checkout>
import fs from "node:fs";
import path from "node:path";
import vm from "node:vm";
import { execFileSync } from "node:child_process";
import { pathToFileURL, fileURLToPath } from "node:url";

const [dir, schwung] = process.argv.slice(2);
const here = path.dirname(fileURLToPath(import.meta.url));
const load = (rel) => import(pathToFileURL(path.join(schwung, "src/shared/param_pages", rel)));
const { planPages } = await load("page_plan.mjs");
const { buildMetaIndex } = await load("param_meta.mjs");
const { resolveViz } = await load("viz.mjs");
const { frameCtx } = await load("frame_ctx.mjs");
const { registerOverlayWidgets, getWidget, clearWidgets } = await load("widget_registry.mjs");

const hierarchy = JSON.parse(fs.readFileSync(path.join(dir, "ui_hierarchy.json"), "utf8"));
const chainParams = JSON.parse(fs.readFileSync(path.join(dir, "chain_params.json"), "utf8"));
const byKey = Object.fromEntries(chainParams.map((p) => [p.key, p]));
const metaIndex = buildMetaIndex({ hierarchy, chainParams });
const moduleJson = JSON.parse(fs.readFileSync(path.join(here, "../src/module.json"), "utf8"));

let fails = 0;
const check = (ok, msg) => { if (!ok) { fails++; console.log("FAIL " + msg); } };

check(moduleJson.capabilities.canvas_script === "canvas.js", "module.json declares canvas.js");
// The generated block (scene names, thumbnails, font) matches the gallery.
try { execFileSync("node", [path.join(here, "../tools/gallery/make_canvas_data.mjs"), "--check"], { stdio: "pipe" }); }
catch (e) { check(false, String(e.stderr || e.message).trim()); }

// Loaded the way the host loads it: a script over a bare global.
const g = {};
g.globalThis = g;
vm.runInNewContext(fs.readFileSync(path.join(here, "../src/canvas.js"), "utf8"), g);
const ov = g.canvas_overlay;
clearWidgets();
const { registered } = registerOverlayWidgets(ov);
check(registered.includes("custom:viz"), `custom:viz registers, got ${registered}`);
const impl = getWidget("custom:viz");

// Every pictured knob has a picture, and canvas.js knows the options Viz
// serves, in order (a value can arrive as an index).
const pictured = chainParams.filter((p) => p.viz);
for (const p of pictured) {
    check(typeof ov._D[ov._roleOf(p.key)] === "function", `${p.key} has a picture`);
    if (p.type === "enum") check(JSON.stringify(ov._optionsOf(p.key)) === JSON.stringify(p.options), `${p.key}: canvas.js knows its ${p.options.length} options`);
}
/* A host ctx that paints a 128x64 screen, so a picture can be compared. */
function screen() {
    const px = new Uint8Array(128 * 64);
    let calls = 0;
    return { px, calls: () => calls, textWidth: (s) => String(s).length * 5,
        fillRect(x, y, w, h, c) { calls++; for (let j = y; j < y + h; j++) for (let i = x; i < x + w; i++) px[j * 128 + i] = c ? 1 : 0; },
        print() {} };
}
function draw(key, values, nowMs, touched) {
    const s = screen();
    const f = frameCtx(s, { x: 0, y: 0, w: 32, h: 15 });
    impl.draw(f, { group: { kind: "custom:viz", keys: [key] }, values, nowMs, touched });
    return { lit: s.px.reduce((a, b) => a + b, 0), clipped: f.clipped(), calls: s.calls(), px: s.px };
}
// An index draws the same as the name it stands for.
{
    ov._reset(); const a = draw("s2_rate", { s2_rate: "1/8" }, 1000).px;
    ov._reset(); const b = draw("s2_rate", { s2_rate: "7" }, 1000).px;
    check(a.every((x, i) => x === b[i]), "an index draws as its option");
}

const SEQ = "3|1R100000|0|4";
const optionsOf = (k) => byKey[k].type === "enum" ? byKey[k].options : [-1, -0.5, 0, 0.5, 1];
const valsFor = (k, v) => ({ seq_now: SEQ, m1_kind: "Sine", m2_kind: "Kick", [k]: v });

const visibleFor = (values) => (cond) => {
    const v = values[cond.param];
    if (v === undefined || cond.equals === undefined) return true;
    return String(v) === String(cond.equals);
};
let worstCalls = 0, worstKey = "", cells = 0;
for (let step = 1; step <= 8; step++) {
    const pages = planPages({ hierarchy, chainParams, visible: visibleFor({ step: String(step) }) }).pages.filter((p) => Array.isArray(p.keys));
    for (const page of pages) {
        const { groups } = resolveViz({ keys: page.keys, metaIndex });
        for (const k of page.keys) {
            if (!byKey[k].viz) continue;
            const grp = groups.find((x) => x.keys.includes(k));
            check(grp && grp.kind === "custom:viz" && grp.slotSpan === 1, `${k} draws as its own Viz cell`);
            if (step > 1 && !/^s\d_/.test(k)) continue;          /* the other pages once is enough */
            for (const v of optionsOf(k)) {
                ov._reset();
                let r;
                try { r = draw(k, valsFor(k, v), 1000); } catch (e) { check(false, `${k}=${v} throws ${e.stack}`); continue; }
                cells++;
                check(r.clipped === 0, `${k}=${v} draws outside its frame (${r.clipped})`);
                check(r.lit >= 5, `${k}=${v} draws a picture (${r.lit} px)`);
                if (r.calls > worstCalls) { worstCalls = r.calls; worstKey = `${k}=${v}`; }
            }
        }
    }
}

// Different settings look different.
const looks = (k, a, b, extra = {}) => { ov._reset(); const p = draw(k, { ...valsFor(k, a), ...extra }, 1000).px; ov._reset(); const q = draw(k, { ...valsFor(k, b), ...extra }, 1000).px; return p.some((x, i) => x !== q[i]); };
for (const p of pictured) {
    const o = optionsOf(p.key);
    for (let i = 1; i < o.length; i++) check(looks(p.key, o[i - 1], o[i]), `${p.key}: ${o[i - 1]} and ${o[i]} look different`);
}

// No answer yet: nothing at all, never a picture of a made-up value.
ov._reset();
check(draw("scene", {}, 1000).lit === 0, "an unanswered value draws nothing");

// STEP and PLAY read the whole sequence from seq_now.
check(looks("step", "3", "3", { seq_now: "1|11000000|8|4" }) === false, "a still STEP replays");
{
    ov._reset(); const a = draw("step", { step: "3", seq_now: "1|11100000|12|4" }, 1000).px;
    ov._reset(); const b = draw("step", { step: "3", seq_now: "2|11100000|12|4" }, 1000).px;
    check(a.some((x, i) => x !== b[i]), "STEP shows which step is playing");
    ov._reset(); const c = draw("step", { step: "3", seq_now: "2|11110000|16|4" }, 1000).px;
    check(b.some((x, i) => x !== c[i]), "STEP shows which steps are on");
    ov._reset(); const d = draw("play", { play: "On", seq_now: "1|11000000|8|4" }, 1000).px;
    ov._reset(); const e = draw("play", { play: "On", seq_now: "1|11100000|12|4" }, 1000).px;
    check(d.some((x, i) => x !== e[i]), "PLAY shows the loop's length");
}
// The LFO's RATE draws its own LFO's shape.
{
    ov._reset(); const a = draw("m1_rate", { m1_rate: "1 bar", m1_kind: "Sine" }, 1000).px;
    ov._reset(); const b = draw("m1_rate", { m1_rate: "1 bar", m1_kind: "Square" }, 1000).px;
    check(a.some((x, i) => x !== b[i]), "LFO 1 RATE follows LFO 1 KIND");
}

// Time controls sleep until turned or touched, then move.
for (const [k, v] of [["rate", "1/4"], ["trail", "Long"], ["m1_rate", "1/4"]]) {
    ov._reset();
    const vals = valsFor(k, v);
    const t0 = draw(k, vals, 1000), t1 = draw(k, vals, 1300);
    check(t0.px.every((x, i) => x === t1.px[i]), `${k} holds still while nobody touches it`);
    const t2 = draw(k, vals, 1400, true), t3 = draw(k, vals, 1700, true);
    check(t2.px.some((x, i) => x !== t3.px[i]), `${k} moves while its knob is touched`);
}
// SCENE draws the scene: two scenes, two pictures; a soft scene draws too.
check(looks("scene", "Pulse", "Rings"), "SCENE draws Pulse and Rings differently");
ov._reset();
check(draw("scene", { scene: "Lava" }, 1000).lit > 20, "a soft scene draws");

console.log(`  ${cells} cells drawn; widest: ${worstCalls} host calls (${worstKey})`);
check(worstCalls <= 160, `every cell reaches the host in at most 160 runs, worst ${worstCalls}`);
console.log(fails ? `FAIL: widgets.test ${fails} failed` : "ok: widgets.test");
process.exit(fails ? 1 : 0);
