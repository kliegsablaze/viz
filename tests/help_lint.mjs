/*
 * help_lint.mjs — the on-device help has a HARD 20-character line limit and
 * the limit is INVISIBLE.
 *
 * Nothing on the device complains about a long line; it is simply cut, and
 * a sentence that lost its last word still reads as a sentence. Copied from
 * Quilt (by way of Ragtag, where the first draft shipped eleven lines at 21
 * characters). The real budget is pixels (MODULES.md, Help Content); twenty
 * characters is the safe count.
 *
 * Checks the structure too: the help renderer walks `children` and `lines`,
 * and a topic with neither is a dead end in the menu that says nothing when
 * you reach it.
 *
 * Run: node tests/help_lint.mjs
 */
import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { dirname, join } from "node:path";

const here = dirname(fileURLToPath(import.meta.url));
const path = join(here, "..", "src", "help.json");

/** The device's own budget. Not a style preference — anything past it is cut. */
const MAX_LINE = 20;
/** Topic titles sit in a menu row, which is narrower still. */
const MAX_TITLE = 18;

let failures = 0;
function fail(msg) { console.error("FAIL: " + msg); failures++; }

let doc;
try {
    doc = JSON.parse(readFileSync(path, "utf8"));
} catch (e) {
    console.error("FAIL: src/help.json is not valid JSON: " + e.message);
    process.exit(1);
}

let topics = 0, lines = 0;

function walk(node, trail) {
    const where = trail.join(" > ");
    if (!node || typeof node !== "object") { fail(`${where}: not an object`); return; }

    if (typeof node.title !== "string" || !node.title) fail(`${where}: no title`);
    else if (node.title.length > MAX_TITLE)
        fail(`${where}: title is ${node.title.length} chars, limit ${MAX_TITLE}`);

    const hasLines = Array.isArray(node.lines);
    const hasKids = Array.isArray(node.children);
    if (!hasLines && !hasKids) fail(`${where}: neither lines nor children — a dead end`);

    if (hasLines) {
        topics++;
        node.lines.forEach((l, i) => {
            lines++;
            if (typeof l !== "string") { fail(`${where} line ${i + 1}: not a string`); return; }
            if (l.length > MAX_LINE)
                fail(`${where} line ${i + 1}: ${l.length} chars, limit ${MAX_LINE}\n      ${JSON.stringify(l)}`);
            if (/[^\x20-\x7e]/.test(l))
                fail(`${where} line ${i + 1}: non-ASCII, which the device draws as a gap\n      ${JSON.stringify(l)}`);
            if (l !== l.trimEnd())
                fail(`${where} line ${i + 1}: trailing space, invisible and it counts`);
        });
    }
    if (hasKids) node.children.forEach((c) => walk(c, trail.concat(c.title || "?")));
}

walk(doc, [doc.title || "?"]);

console.log(`${lines} lines across ${topics} topics, limit ${MAX_LINE}`);
if (failures) { console.error(`SUITE FAILED: ${failures} problem(s)`); process.exit(1); }
console.log("PASS: src/help.json (every line inside the device's budget)");
