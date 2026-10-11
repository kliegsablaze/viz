// Plans Viz's pages with the host's own planner, runs the host's contract
// validator, and checks every cell word draws as typed (labelVerbatim).
// Usage: node plan.test.mjs <dump dir> <schwung checkout>
import fs from "node:fs";
import path from "node:path";
import { pathToFileURL } from "node:url";

const [dir, schwung] = process.argv.slice(2);
const load = (rel) => import(pathToFileURL(path.join(schwung, "src/shared/param_pages", rel)));
const { planPages } = await load("page_plan.mjs");
const { validateContract } = await load("validate_contract.mjs");
const { labelVerbatim, drawHeader } = await load("render_page_movy.mjs");

const hierarchy = JSON.parse(fs.readFileSync(path.join(dir, "ui_hierarchy.json"), "utf8"));
const chainParams = JSON.parse(fs.readFileSync(path.join(dir, "chain_params.json"), "utf8"));

let fails = 0;
const check = (ok, msg) => { if (!ok) { fails++; console.log("FAIL " + msg); } };

// DESIGN.md, Control surface: every cell is a real word of at most five letters.
for (const p of chainParams) {
    check(typeof p.short_name === "string" && p.short_name.length > 0 && p.short_name.length <= 5, `${p.key}: cell word "${p.short_name}"`);
    check(typeof p.name === "string" && p.name.length > 0, `${p.key} declares a header name`);
    const drawn = labelVerbatim(p.short_name);
    check(drawn === p.short_name.toUpperCase(), `${p.key}: cell "${p.short_name}" draws as "${drawn}"`);
}

// Four pages: Look, SEQ, MOD, Settings. The Sequence page shows the
// chosen step's knobs only, through gates on STEP (the shadow UI's
// comparison, compareConditionValue: an exact string match).
const visibleFor = (values) => (cond) => {
    const v = values[cond.param];
    if (v === undefined) return true;
    if (cond.equals !== undefined) return String(v) === String(cond.equals);
    if (cond.not_equals !== undefined) return String(v) !== String(cond.not_equals);
    return true;
};
const look = ["scene", "color", "rate", "size", "trail", "fold", "shift", "react"];
const settings = ["mode", "power", "light", "grid", "cycle", "turn", "edge", "nudge"];
const modKeys = ["m1_kind", "m1_rate", "m1_aim", "m1_depth", "m2_kind", "m2_rate", "m2_aim", "m2_depth"];
for (let step = 1; step <= 8; step++) {
    const plan = planPages({ hierarchy, chainParams, visible: visibleFor({ step: String(step) }) });
    const pages = plan.pages.filter((p) => Array.isArray(p.keys));
    const seq = ["step", `s${step}_scene`, `s${step}_bars`, "play", `s${step}_color`, `s${step}_rate`, `s${step}_size`, `s${step}_fold`];
    check(pages.length === 4, `step ${step}: 4 knob pages, got ${pages.length}: ${pages.map((p) => JSON.stringify(p.keys)).join(" ")}`);
    check(JSON.stringify(pages[0]?.keys) === JSON.stringify(look), `page 1 is ${JSON.stringify(pages[0]?.keys)}`);
    check(JSON.stringify(pages[1]?.keys) === JSON.stringify(seq), `step ${step}: page 2 is ${JSON.stringify(pages[1]?.keys)}`);
    check(JSON.stringify(pages[2]?.keys) === JSON.stringify(modKeys), `page 3 is ${JSON.stringify(pages[2]?.keys)}`);
    check(JSON.stringify(pages[3]?.keys) === JSON.stringify(settings), `page 4 is ${JSON.stringify(pages[3]?.keys)}`);
}
// Schwung's automation mark clears x 121..127 of the header (shadow_ui.js
// drawPlockMark); every page name ends left of it, and none is the host's "Main".
{
    const pages = planPages({ hierarchy, chainParams, visible: visibleFor({ step: "1" }) }).pages.filter((p) => Array.isArray(p.keys));
    check(pages[0]?.name === "Look  ", `the first page is Look, got ${JSON.stringify(pages[0]?.name)}`);
    for (const pg of pages) {
        let right = 0;
        drawHeader({ fillRect: (x, y, w, h, c) => { if (c && w < 128) right = Math.max(right, x + w - 1); } }, "", pg.name);
        check(right > 100 && right < 121, `page "${pg.name}" ends at x ${right}, clear of the mark`);
    }
}
// Every gate names a real STEP option, and the pictures ask for at most four extra keys.
const stepOpts = chainParams.find((p) => p.key === "step")?.options || [];
for (const lv of Object.values(hierarchy.levels))
    for (const e of lv.params || []) if (e.visible_if)
        check(e.visible_if.param === "step" && stepOpts.includes(e.visible_if.equals), `${e.key}: gate ${JSON.stringify(e.visible_if)}`);
for (const p of chainParams) {
    const ek = p.viz?.extra_keys || [];
    check(ek.length <= 4, `${p.key}: ${ek.length} extra keys`);
    if (p.viz) check(p.viz.kind === "custom:viz", `${p.key}: picture kind ${p.viz.kind}`);
}
check(!chainParams.some((p) => settings.includes(p.key) && p.viz), "Settings keeps the host's own pictures");

const mod = JSON.parse(fs.readFileSync(path.join(path.dirname(new URL(import.meta.url).pathname), "../src/module.json"), "utf8"));
const { findings } = validateContract({ id: "viz", hierarchy, chainParams, capabilities: mod.capabilities });
for (const f of findings) if (f.level !== "info") console.log(`  validate ${f.level}: ${f.rule} — ${f.message}`);
check(!findings.some((f) => f.level === "error"), "the host's validator reports no errors");

console.log(fails ? `FAIL: plan.test ${fails} failed` : "ok: plan.test");
process.exit(fails ? 1 : 0);
