/*
 * make_pages.mjs — build build/pages.html, the mockup of Viz's Sequence and Mod
 * pages, from pages.src.html plus the scene gallery's own engine (everything in
 * gallery.html from "music" up to "page"), so the mockup's Matrix plays the
 * same scenes the Move does.
 *
 * font4x5.json is the device's label font (Schwung, src/shared/param_pages/
 * font4x5.mjs, MIT), unpacked to pixels so the screen reads as the Move's does.
 *
 * Run: node tools/gallery/make_pages.mjs   then open build/pages.html
 */
import { readFileSync, writeFileSync, mkdirSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { dirname, join } from "node:path";

const here = dirname(fileURLToPath(import.meta.url));
const root = join(here, "..", "..");
const gallery = readFileSync(join(here, "gallery.html"), "utf8");
const from = gallery.indexOf("/* ---------------- music"), to = gallery.indexOf("/* ---------------- page");
if (from < 0 || to < 0) { console.error("FAIL: gallery markers not found"); process.exit(1); }
const src = readFileSync(join(here, "pages.src.html"), "utf8");
const font = readFileSync(join(here, "font4x5.json"), "utf8");
const out = src.replace("/*@ENGINE@*/", () => gallery.slice(from, to)).replace("/*@FONT@*/null", () => font);
mkdirSync(join(root, "build"), { recursive: true });
writeFileSync(join(root, "build", "pages.html"), out);
console.log("wrote build/pages.html");
