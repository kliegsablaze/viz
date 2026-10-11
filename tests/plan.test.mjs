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
const { labelVerbatim } = await load("render_page_movy.mjs");

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

// Two pages, Look then Settings, eight knobs each, in the designed order.
const pages = planPages({ hierarchy, chainParams, visible: () => true }).pages.filter((p) => Array.isArray(p.keys));
const look = ["scene", "color", "flow", "grow", "bloom", "react", "warp", "drift"];
const settings = ["mode", "power", "light", "grid", "cycle", "turn", "edge", "nudge"];
check(pages.length === 2, `2 knob pages, got ${pages.length}: ${pages.map((p) => p.title).join(", ")}`);
check(JSON.stringify(pages[0]?.keys) === JSON.stringify(look), `page 1 is ${JSON.stringify(pages[0]?.keys)}`);
check(JSON.stringify(pages[1]?.keys) === JSON.stringify(settings), `page 2 is ${JSON.stringify(pages[1]?.keys)}`);

const mod = JSON.parse(fs.readFileSync(path.join(path.dirname(new URL(import.meta.url).pathname), "../src/module.json"), "utf8"));
const { findings } = validateContract({ id: "viz", hierarchy, chainParams, capabilities: mod.capabilities });
for (const f of findings) if (f.level !== "info") console.log(`  validate ${f.level}: ${f.rule} — ${f.message}`);
check(!findings.some((f) => f.level === "error"), "the host's validator reports no errors");

console.log(fails ? `FAIL: plan.test ${fails} failed` : "ok: plan.test");
process.exit(fails ? 1 : 0);
