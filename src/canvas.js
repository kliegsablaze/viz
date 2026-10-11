/*
 * Viz's knob pictures (DESIGN.md, "Knob pictures"). Schwung loads this file
 * because module.json names it, and calls drawCell for every cell whose
 * chain_params entry says `viz: {kind: "custom:viz"}`: the Look, Sequence and
 * Mod pages. Settings keeps the host's own.
 *
 * Drawn as approved in the page mockup (tools/gallery/pages.src.html, the user,
 * 2026-10-11), in the same hand as Strut and Quilt:
 *
 *   ONE PEN. One-pixel lines and dots, in the cell's own pixels (32x15).
 *   MOTION MEANS TIME. Only RATE, TRAIL and the LFOs' RATE move, and only
 *     while their knob is touched or for 1.5 s after it turns.
 *   SAY IT IN PICTURES. SCENE draws the scene itself, a dot a pad. Words
 *     only where a word is the clearest picture (a colour set's name, 1/4).
 *
 * The block between the @generated markers comes from the scene gallery
 * (tools/gallery/make_canvas_data.mjs): the scene names, each scene's
 * thumbnail, and the device's 4x5 label font.
 */
(function () {
const W = 32, H = 15, AWAKE_MS = 1500, TAU = Math.PI * 2;

/* @generated begin */
const SCENE_NAMES = ["Pulse","Rings","Diamond","Burst","Sweep","Cross","Snake","Radar","Stripes","Tunnel","Quads","Shapes","Steps","Rise","Bounce","Wave","Rain","Bars","Spiral","Pinwheel","Kit","Icons","Count","Arrow","Corners","Stack","Rocket","Halves","Pong","Trace","Invaders","Breakout","Text","Spin","Heart","Sand","Chomp","Doors","Flap","Jump","Noughts","Four","Road","Maze","Dice","Catch","Lander","Tanks","Face","Eye","Sun","Moon","Flower","Tick","Speaker","Saturn","Clock","Charge","UFO","Wipe","Curtain","Iris","Pie","Diag","Zoom","Aim","Propeller","Eight","Helix","Metronome","Cradle","Piston","Ping","Juggle","Frame","Stairs","Swap","Bubbles","Train","Boxes","Bass","Pads","Tally","Beats","Lift","Hoop","Bowl","Darts","Seesaw","Yoyo","Coil","Volley","Swing","Goal","Skate","Mow","Pour","Ski","Pool","Weave","Aurora","Nebula","Tide","Fireflies","Reeds","Lava"];
const THUMBS = ["00003c3c3c3c0000","007e7e66667e7e00","183c66c3c3663c18","003f7f7f7f7f3e01","c0c0c0c0c0c0c0c0","c3e77e3c3c7ee7c3","7f01000000000000","000000000f070301","cc993366cc993366","ff818199998181ff","0000303000000000","00183c66663c1800","c0c0c0c0c0c0cfcf","00000000ffffffff","000000000000c0c0","00183c7cc7c38100","b4b4b0b1b13b3a3a","00c0c0f0ffffffff","f88406366c60211f","3c181800ffe7e7c3","c3c33c3c3c3cc3c3","66ffffff7e3c1800","003e203c0202221c","183c7eff18181818","c3810000000081c3","0e001ce73fe7eefc","1c36634163361c00","e0e0e0e0e0e0e0e0","000081c181000000","3333337755dd1919","7755007755101038","ffff000000100038","00df8182848890df","303c6743c2e63c0c","66ffffff7e3c1800","ff7e3c1808000000","000070f8fdf87000","e7e7e7e7e7e7e7e7","8828000080888888","0000000000c1c1ff","dbdb00dbdb00dbdb","04000004446464fc","292121528484b438","80fe007f00fe00ff","c3c300c3c300c3c3","8000000000000038","182418000000003c","00101824000424e7","3c42ad81a599423c","003c42b1b1423c00","181818ffff181818","187e7effff7e7e18","3c1899ffff99183c","000103466c381000","0c3efef6f6fe3e0c","0000f8fc3f1f0000","00000808f8000000","00fefafbfbfafe00","3078fc30307878fc","ffffffffffffffff","ffffffffffffffff","ffffffffffffffff","ffffffffffffffff","ffffffffffffffff","ffffc3c3c3c3ffff","ff10101010101010","0404c8381c132020","0020101008000000","0066118888116600","0001010206040418","0000000000003c00","c0c0f3f3f3f3f3ff","c0c0000000000000","0008040000004000","ffffc3c3c3c3ffff","03030f0f3f3fffff","e0e0e00000070707","00c0c00000006060","0040400040403000","0000ff9999ff0000","000000ffffffffff","c0c00000ffffffff","ffffffffff000000","c0c0c0c0c0c0c0c0","0000000000003c3c","0000060602004000","5a18000000001818","ffffc3dbdbc3ffff","0000008068180e19","1818180000000000","ffffffffffffffff","0000000000004042","0000000000030000","3c3c180000000008","8080000000000000","ffffffffffffffff","ffffffffffffffff","6020000006000000","0100000800004000","ffffffffffffffff"];
const FONT = {"0":[4,6,9,9,9,6],"1":[3,2,3,2,2,7],"2":[4,7,8,6,1,15],"3":[4,7,8,6,8,7],"4":[4,9,9,15,8,8],"5":[4,15,1,7,8,7],"6":[4,14,1,7,9,6],"7":[4,15,8,4,2,1],"8":[4,6,9,6,9,6],"9":[4,6,9,14,8,7]," ":[2,0,0,0,0,0],"!":[1,1,1,1,0,1],"\"":[3,5,5,0,0,0],"'":[1,1,1,0,0,0],"(":[2,2,1,1,1,2],")":[2,1,2,2,2,1],"+":[3,0,2,7,2,0],",":[2,0,0,0,2,1],"-":[3,0,0,7,0,0],".":[1,0,0,0,0,1],"/":[4,8,8,6,1,1],":":[1,0,1,0,1,0],"A":[4,6,9,15,9,9],"B":[4,7,9,7,9,7],"C":[4,14,1,1,1,14],"D":[4,7,9,9,9,7],"E":[4,15,1,7,1,15],"F":[4,15,1,7,1,1],"G":[4,14,1,13,9,14],"H":[4,9,9,15,9,9],"I":[1,1,1,1,1,1],"J":[4,12,8,8,9,6],"K":[4,9,5,3,5,9],"L":[4,1,1,1,1,15],"M":[5,17,27,21,17,17],"N":[4,9,11,13,9,9],"O":[4,6,9,9,9,6],"P":[4,7,9,7,1,1],"Q":[4,6,9,9,5,10],"R":[4,7,9,7,5,9],"S":[4,14,1,6,8,7],"T":[3,7,2,2,2,2],"U":[4,9,9,9,9,6],"V":[5,17,17,17,10,4],"W":[5,17,17,21,21,10],"X":[4,9,9,6,9,9],"Y":[3,5,5,2,2,2],"Z":[4,15,8,6,1,15],"%":[4,9,8,6,1,9],"<":[3,4,2,1,2,4],">":[3,1,2,4,2,1],"=":[3,0,7,0,7,0],"?":[4,7,8,6,0,2],"*":[3,5,2,5,0,0],"#":[4,10,15,10,15,10],"&":[4,6,9,6,5,10],"_":[4,0,0,0,0,15],"\\":[4,1,1,6,8,8],"^":[3,2,5,0,0,0]};
const LUMA = {"Ice":[0.32,0.62,0.89],"Fire":[0.36,0.6,0.84],"Acid":[0.72,0.38,0.68],"Sunset":[0.35,0.58,0.27],"Mint":[0.64,0.46,0.94],"Royal":[0.25,0.73,0.27],"Candy":[0.54,0.64,0.87],"Mono":[1,0.55,0.28]};
/* @generated end */

/* ---------------------------------------------------------- the options -- */
const COLORS = ["Ice", "Fire", "Acid", "Sunset", "Mint", "Royal", "Candy", "Mono", "Rainbow"];
const RATES = ["4 bars", "2 bars", "1 bar", "1/2", "1/4", "1/8", "1/16"];
const TICKS = [1, 1, 1, 2, 4, 8, 16];            /* ticks drawn in one bar (4 and 2 bars: one, longer) */
const SIZES = ["1", "2", "3", "4"];
const TRAILS = ["Off", "Short", "Medium", "Long", "Smear"];
const FOLDS = ["Off", "Sides", "Top", "Four", "Eight"];
const SHIFTS = ["Hold", "Bar", "Beat", "Kick"];
const REACTS = ["Off", "Glow", "Grow", "Flash", "Flip"];
const BARS = ["Off", "Random", "1 beat", "2 beats", "1 bar", "2 bars", "4 bars", "8 bars"];
const KINDS = ["Sine", "Ramp", "Square", "Random", "Kick", "Level"];
const MRATES = ["1/16", "1/8", "1/4", "1/2", "1 bar", "2 bars", "4 bars", "8 bars", "16 bars"];
const MRATE_BEATS = [0.25, 0.5, 1, 2, 4, 8, 16, 32, 64];
const AIMS = ["Scene", "Color", "Rate", "Size", "Trail", "Fold", "Shift", "React", "Light", "Turn"];

/* ------------------------------------------------------------- the pen -- */
const BAYER = [0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5];
function textW(s) { let w = 0; for (const ch of s) { const g = FONT[ch] || FONT[" "]; w += (g ? g[0] : 3) + 1; } return Math.max(0, w - 1); }
function pen() {
    const bm = new Uint8Array(W * H);
    const p = {
        px(x, y) { x = Math.round(x); y = Math.round(y); if (x >= 0 && x < W && y >= 0 && y < H) bm[y * W + x] = 1; },
        clr(x, y) { if (x >= 0 && x < W && y >= 0 && y < H) bm[y * W + x] = 0; },
        h(x0, x1, y) { for (let x = Math.min(x0, x1); x <= Math.max(x0, x1); x++) p.px(x, y); },
        v(x, y0, y1) { for (let y = Math.min(y0, y1); y <= Math.max(y0, y1); y++) p.px(x, y); },
        box(x0, y0, x1, y1) { p.h(x0, x1, y0); p.h(x0, x1, y1); p.v(x0, y0, y1); p.v(x1, y0, y1); },
        fill(x0, y0, x1, y1) { for (let y = y0; y <= y1; y++) p.h(x0, x1, y); },
        dots(x0, x1, y, every) { for (let x = x0; x <= x1; x += every || 2) p.px(x, y); },
        dither(x0, y0, x1, y1, lvl) { for (let y = y0; y <= y1; y++) for (let x = x0; x <= x1; x++) if (BAYER[(y & 3) * 4 + (x & 3)] < lvl * 16) p.px(x, y); },
        line(x0, y0, x1, y1) { const n = Math.max(Math.abs(x1 - x0), Math.abs(y1 - y0), 1); for (let i = 0; i <= n; i++) p.px(x0 + (x1 - x0) * i / n, y0 + (y1 - y0) * i / n); },
        curve(x0, x1, f) { let py = null; for (let x = x0; x <= x1; x++) { const y = Math.round(f(x)); if (py !== null && Math.abs(y - py) > 1) p.v(x, py + Math.sign(y - py), y); p.px(x, y); py = y; } },
        text(s, y, x) {
            s = String(s).toUpperCase();
            let cx = x != null ? x : Math.round((W - textW(s)) / 2);
            for (const ch of s) { const g = FONT[ch] || FONT[" "]; if (!g) { cx += 4; continue; }
                for (let r = 0; r < 5; r++) for (let i = 0; i < g[0]; i++) if (g[1 + r] >> i & 1) p.px(cx + i, y + r);
                cx += g[0] + 1; }
        },
        runs() {
            const out = [];
            for (let j = 0; j < H; j++) for (let i = 0; i < W; i++) {
                if (!bm[j * W + i]) continue;
                const a = i; while (i < W && bm[j * W + i]) i++;
                out.push(a, j, i - a);
            }
            return out;
        },
    };
    return p;
}
function arrowR(p, x, y) { p.px(x - 1, y - 1); p.px(x - 1, y + 1); p.px(x - 2, y - 2); p.px(x - 2, y + 2); }
function die(p, x, y) {                           /* Random: a die, as Strut's DICE */
    p.box(x, y, x + 8, y + 8);
    [[2, 2], [6, 2], [4, 4], [2, 6], [6, 6]].forEach(([dx, dy]) => p.px(x + dx, y + dy));
}
function same(p) { p.text("SAME", 2); p.dots(6, 25, 10, 2); p.px(16, 11); p.px(15, 12); p.px(17, 12); }

/* ------------------------------------------------------------ pictures -- */
const D = {};
D.scene = (p, s) => {                             /* the scene, a dot a pad, and its number */
    if (s.opt === "Random") { die(p, 4, 3); p.text("ANY", 5, 17); return; }
    const i = SCENE_NAMES.indexOf(s.opt);
    const hex = THUMBS[i];
    if (hex) {
        for (let k = 0; k < 64; k++) if (parseInt(hex[k >> 2], 16) >> (3 - (k & 3)) & 1) p.px(2 + (k % 8) * 2, Math.floor(k / 8) * 2);
    } else {                                      /* a soft scene: a glow, not a shape */
        for (let y = 0; y < 15; y++) for (let x = 1; x < 17; x++) { const d = Math.hypot(x - 8.5, (y - 7) * 1.1) / 7;
            if (d < 1 && BAYER[(y & 3) * 4 + (x & 3)] < (1 - d) * 18) p.px(x, y); }
    }
    p.px(1, 0); p.px(1, 14); p.px(17, 0); p.px(17, 14);
    const n = i + 1;
    p.text(String(n), 5, 20 + (n < 10 ? 3 : n < 100 ? 0 : -1));
};
D.color = (p, s) => {                             /* three swatches as bright as their colours, and the name */
    if (s.opt === "Same") return same(p);
    if (s.opt === "Random") { die(p, 3, 3); p.text("ANY", 5, 16); return; }
    const L = LUMA[s.opt];
    if (!L) { for (let x = 3; x <= 28; x++) for (let y = 0; y <= 5; y++) if (BAYER[(y & 3) * 4 + (x & 3)] < (0.5 + 0.45 * Math.sin(x * 0.55)) * 16) p.px(x, y); }
    else L.forEach((l, i) => { const x0 = 4 + i * 9; p.box(x0, 0, x0 + 6, 5); p.dither(x0 + 1, 1, x0 + 5, 4, 0.15 + 0.85 * l); });
    p.text(s.opt, 9);
};
D.rate = (p, s) => {                              /* one bar, a tick a step; a dot walks them while awake */
    if (s.opt === "Same") return same(p);
    if (s.opt === "Random") { die(p, 3, 3); p.text("ANY", 5, 16); return; }
    const r = RATES.indexOf(s.opt), n = TICKS[r];
    p.text(s.opt.replace(" bars", "BAR").replace(" bar", "BAR"), 0);
    p.h(3, 28, 14);
    if (r < 2) { p.v(3, 9, 13); p.dots(5, 27, 12, 3); p.v(28, 9, 13); return; }   /* longer than the bar */
    for (let i = 0; i < n; i++) { const x = 3 + Math.round(i * 25 / n); p.v(x, i === 0 ? 9 : 11, 13); }
    p.v(28, 9, 13);
    if (s.awake) { const steps = n, i = Math.floor(s.now / 500 * steps / 4) % steps, x = 3 + Math.round(i * 25 / steps); p.px(x, 7); p.px(x + 1, 7); }
};
D.size = (p, s) => {                              /* a 4x4 corner of the grid, the shape's size filled */
    if (s.opt === "Same") return same(p);
    if (s.opt === "Random") { die(p, 3, 3); p.text("ANY", 5, 16); return; }
    const n = SIZES.indexOf(s.opt) + 1;
    for (let j = 0; j < 4; j++) for (let i = 0; i < 4; i++) { const x = 10 + i * 3, y = 1 + j * 3;
        if (i < n && j < n) p.fill(x, y, x + 1, y + 1); else p.px(x, y); }
    p.text(String(n), 5, 24);
};
D.trail = (p, s) => {                             /* a light and the trail it leaves */
    const v = Math.max(0, TRAILS.indexOf(s.opt)), L = [0, 4, 9, 15, 22][v], t = s.now / 1000;
    const x0 = s.awake ? 6 + Math.round((0.5 + 0.5 * Math.sin(t * 3)) * 20) : 26, y = 6;
    const dir = s.awake ? Math.sign(Math.cos(t * 3)) || 1 : 1;
    p.fill(x0, y, x0 + 1, y + 1);
    for (let k = 1; k <= L; k++) {                /* solid near the light, sparser further back */
        const every = k <= L / 3 ? 1 : k <= 2 * L / 3 ? 2 : 3, x = x0 - k * dir + (dir < 0 ? 1 : 0);
        if (k % every === 0) p.px(x, y + (k <= L / 3 ? 1 : 0));
        if (k <= L / 3) p.px(x, y);
    }
    p.text(s.opt, 10);
};
function foldBits(m) {                            /* a mark in the corner of a 12x12 grid, folded as the engine does */
    const mark = [[1, 1], [2, 1], [3, 1], [4, 1], [1, 2], [1, 3], [3, 3], [4, 4], [2, 4]];
    const src = new Uint8Array(144); mark.forEach(([x, y]) => (src[y * 12 + x] = 1));
    if (m <= 0) return src;
    const out = new Uint8Array(144);
    for (let y = 0; y < 12; y++) for (let x = 0; x < 12; x++) { let sx = x, sy = y;
        if ((m === 1 || m >= 3) && sx > 5) sx = 11 - sx;
        if ((m === 2 || m >= 3) && sy > 5) sy = 11 - sy;
        if (m === 4 && sx > sy) { const q = sx; sx = sy; sy = q; }
        out[y * 12 + x] = src[sy * 12 + sx]; }
    return out;
}
D.fold = (p, s) => {
    if (s.opt === "Same") return same(p);
    if (s.opt === "Random") { die(p, 3, 3); p.text("ANY", 5, 16); return; }
    const v = FOLDS.indexOf(s.opt), b = foldBits(v), ox = 10, oy = 1;
    for (let y = 0; y < 12; y++) for (let x = 0; x < 12; x++) if (b[y * 12 + x]) p.px(ox + x, oy + y);
    if (v === 1 || v >= 3) for (let y = 0; y < 13; y += 2) p.px(ox + 5.5, oy + y);
    if (v === 2 || v >= 3) for (let x = 0; x < 13; x += 2) p.px(ox + x, oy + 5.5);
    if (v === 4) for (let k = 0; k < 12; k += 2) p.px(ox + k, oy + k);
    p.box(ox - 2, oy - 1, ox + 12, oy + 12);
};
D.shift = (p, s) => {                             /* three colours trading places, and when */
    const xs = [5, 13, 21];
    p.fill(xs[0], 0, xs[0] + 5, 4); p.dither(xs[1], 0, xs[1] + 5, 4, 0.5); p.box(xs[2], 0, xs[2] + 5, 4);
    if (s.opt !== "Hold") { p.h(8, 24, 6); arrowR(p, 24, 6); }
    p.text(s.opt, 9);
};
D.react = (p, s) => {                             /* a kick, and what it does to a shape */
    const v = REACTS.indexOf(s.opt);
    p.v(3, 2, 12); p.curve(3, 10, (x) => 12 - 10 * Math.exp(-(x - 3) / 1.6));
    p.h(12, 15, 7); arrowR(p, 15, 7);
    const x0 = 18, y0 = 1, x1 = 30, y1 = 13, cx = 24, cy = 7;
    if (v === 3) { p.dither(x0, y0, x1, y1, 0.35); p.fill(cx - 1, cy - 1, cx + 1, cy + 1); return; }
    if (v === 4) { p.fill(x0, y0, x1, y1); for (let y = cy - 1; y <= cy + 1; y++) for (let x = cx - 1; x <= cx + 1; x++) p.clr(x, y); return; }
    p.box(x0, y0, x1, y1);
    p.fill(cx - 1, cy - 1, cx + 1, cy + 1);
    if (v === 1) [[0, -3], [0, 3], [-3, 0], [3, 0], [-3, -3], [3, 3], [-3, 3], [3, -3]].forEach(([dx, dy]) => p.px(cx + dx, cy + dy));
    if (v === 2) { p.fill(cx - 2, cy - 2, cx + 2, cy + 2); p.box(cx - 4, cy - 4, cx + 4, cy + 4); }
};

/* The whole sequence, from the read-only "seq_now": playing|steps|beats|bar. */
function seqNow(values) {
    const parts = String(values.seq_now || "").split("|");
    return { playing: (parseInt(parts[0], 10) || 0) - 1, steps: parts[1] || "10000000",
        beats: parseFloat(parts[2]) || 0, bar: parseInt(parts[3], 10) || 4 };
}
D.step = (p, s) => {                              /* the eight steps: edited solid, skipped a dot, playing underlined */
    const sel = parseInt(s.opt, 10) - 1, q = seqNow(s.values);
    for (let i = 0; i < 8; i++) {
        const x = 1 + i * 4, on = q.steps[i] && q.steps[i] !== "0";
        if (i === sel) p.fill(x, 2, x + 2, 10);
        else if (on) p.box(x, 2, x + 2, 10);
        else p.px(x + 1, 10);
        if (i === q.playing) p.h(x, x + 2, 13);
    }
};
D.bars = (p, s) => {                              /* how long the step plays */
    const v = BARS.indexOf(s.opt);
    if (v <= 0) { p.text("OFF", 1); p.dots(3, 28, 11, 2); return; }
    if (v === 1) { die(p, 3, 3); p.text("ANY", 5, 16); return; }
    const w = Math.round((v - 1) / 6 * 25);
    p.text(s.opt.replace(" beats", "BT").replace(" beat", "BT").replace(" bars", "BAR").replace(" bar", "BAR"), 0);
    p.fill(3, 9, 3 + w, 11); p.dots(4 + w, 28, 11, 2); p.v(3, 8, 12);
};
D.play = (p, s) => {                              /* a play sign; on, the loop's length in bars */
    const tri = (x0, solid) => { for (let k = 0; k <= 5; k++) { if (solid) p.v(x0 + k, 1 + k, 11 - k); else { p.px(x0 + k, 1 + k); p.px(x0 + k, 11 - k); } } if (!solid) p.v(x0, 1, 11); };
    if (s.opt !== "On") { tri(13, false); return; }
    const q = seqNow(s.values);
    tri(4, true);
    if (!q.beats) { p.text("ANY", 4, 14); return; }
    const bars = q.beats / q.bar;
    p.text(Number.isInteger(bars) ? String(bars) : String(q.beats), 1, 14);
    p.text(Number.isInteger(bars) ? "BAR" : "BT", 8, 14);
};

/* THE MOD PAGE ------------------------------------------------------------ */
function hash01(i) { let x = (i * 2654435761) >>> 0; x ^= x >>> 15; x = Math.imul(x, 2246822519) >>> 0; x ^= x >>> 13; return (x >>> 0) / 4294967296; }
function lfoShape(kind, u) {                      /* one cycle, -1..1 (Kick and Level 0..1) */
    const ph = u - Math.floor(u), n = Math.floor(u);
    if (kind === 0) return Math.sin(ph * TAU);
    if (kind === 1) return ph * 2 - 1;
    if (kind === 2) return ph < 0.5 ? 1 : -1;
    if (kind === 3) return hash01(n + 3) * 2 - 1;
    if (kind === 4) return Math.exp(-4 * ph);
    return 0.35 + 0.3 * Math.sin(u * 9.1) * Math.sin(u * 3.3 + 1) + 0.2 * hash01(Math.floor(u * 8));
}
D.kind = (p, s) => {
    const v = Math.max(0, KINDS.indexOf(s.opt)), mid = 7;
    if (v === 4) { for (const x0 of [4, 16]) { p.v(x0, 2, 12); p.curve(x0, x0 + 11, (x) => 12 - 10 * Math.exp(-(x - x0) / 2.5)); } p.h(3, 28, 13); return; }
    if (v === 5) { for (let x = 3; x <= 28; x += 2) { const h = Math.round(2 + 9 * Math.abs(Math.sin(x * 0.7) * Math.sin(x * 0.23 + 1))); p.v(x, 12 - h, 12); } return; }
    p.dots(3, 28, mid, 3);
    p.curve(3, 28, (x) => mid - 5 * lfoShape(v, (x - 3) / 25 * 2 + 0.0001));
};
D.mrate = (p, s) => {                             /* the LFO's shape at its speed, moving while awake */
    const kk = s.param.slice(0, 3) + "kind", kind = Math.max(0, KINDS.indexOf(s.values[kk] == null ? "" : optionName(kk, s.values[kk])));
    const v = Math.max(0, MRATES.indexOf(s.opt));
    const cyc = Math.max(0.5, Math.min(4, 2 / Math.sqrt(MRATE_BEATS[v])));
    const ph = s.awake ? s.now / 500 / MRATE_BEATS[v] : 0;
    p.curve(3, 28, (x) => { let m = lfoShape(kind, ((x - 3) / 25) * cyc - ph); if (kind >= 4) m = m * 2 - 1; return 3 - 2.5 * m; });
    p.text(s.opt.replace(" bars", "BAR").replace(" bar", "BAR"), 9);
};
const MINI = {                                    /* a small picture of each target, right of the arrow */
    Scene(p) { p.box(14, 1, 26, 13); for (let y = 0; y < 4; y++) for (let x = 0; x < 4; x++) if ((x + y) % 3 === 0) p.fill(16 + x * 3, 3 + y * 3, 17 + x * 3, 4 + y * 3); },
    Color(p) { p.fill(14, 4, 17, 9); p.dither(20, 4, 23, 9, 0.5); p.box(26, 4, 29, 9); },
    Rate(p) { p.h(14, 29, 12); for (let x = 14; x <= 29; x += 5) p.v(x, x === 14 ? 6 : 9, 11); },
    Size(p) { p.fill(19, 5, 22, 8); p.box(16, 2, 25, 11); },
    Trail(p) { p.fill(27, 6, 28, 7); for (let x = 14; x < 27; x++) if (x % 2 || x > 22) p.px(x, 6); },
    Fold(p) { const b = foldBits(3); for (let y = 0; y < 12; y++) for (let x = 0; x < 12; x++) if (b[y * 12 + x]) p.px(15 + x, 1 + y); },
    Shift(p) { p.fill(14, 2, 18, 6); p.box(24, 2, 28, 6); p.line(16, 8, 26, 12); p.line(26, 8, 16, 12); },
    React(p) { p.v(16, 2, 12); p.curve(16, 28, (x) => 12 - 10 * Math.exp(-(x - 16) / 2)); },
    Light(p) { const cx = 21, cy = 7; p.box(cx - 2, cy - 2, cx + 2, cy + 2); [[0, -5], [0, 5], [-5, 0], [5, 0], [-4, -4], [4, 4], [-4, 4], [4, -4]].forEach(([dx, dy]) => { p.px(cx + dx, cy + dy); p.px(cx + dx * 0.8, cy + dy * 0.8); }); },
    Turn(p) { const cx = 21, cy = 7, r = 5; for (let a = 0.3; a < 5.6; a += 0.2) p.px(cx + r * Math.cos(a), cy + r * Math.sin(a)); p.px(cx + 5, cy - 2); p.px(cx + 6, cy - 3); p.px(cx + 4, cy - 3); },
};
D.aim = (p, s) => { p.h(2, 9, 7); arrowR(p, 9, 7); (MINI[s.opt] || MINI.Scene)(p); };
D.depth = (p, s) => {                             /* how far, and which way, from the middle */
    const d = Math.round(Math.max(-1, Math.min(1, s.num)) * 12);
    p.v(16, 2, 12);
    if (d) { const x1 = 16 + d, sg = Math.sign(d); p.h(16, x1, 6); p.h(16, x1, 8); p.v(x1, 5, 9); p.px(x1 + sg, 7); }
    p.dots(4, 28, 13, 3);
};

/* ------------------------------------------------------------- routing -- */
function roleOf(key) {
    const m = /^s\d_(scene|bars|color|rate|size|fold)$/.exec(key);
    if (m) return m[1];
    const l = /^m\d_(kind|rate|aim|depth)$/.exec(key);
    if (l) return l[1] === "rate" ? "mrate" : l[1];
    return key;
}
/* Each knob's options, in the order Viz serves them, so a value that arrives
 * as an index still names the right one. */
const RS = ["Random", "Same"];
function optionsOf(key) {
    const step = /^s\d_/.test(key), role = roleOf(key);
    switch (role) {
    case "scene": return step ? ["Random", ...SCENE_NAMES] : SCENE_NAMES;
    case "color": return step ? [...RS, ...COLORS] : COLORS;
    case "rate":  return step ? [...RS, ...RATES] : RATES.slice(2);
    case "size":  return step ? [...RS, ...SIZES] : SIZES;
    case "fold":  return step ? [...RS, ...FOLDS] : FOLDS;
    case "trail": return TRAILS;
    case "shift": return SHIFTS;
    case "react": return REACTS;
    case "bars":  return BARS;
    case "kind":  return KINDS;
    case "mrate": return MRATES;
    case "aim":   return AIMS;
    case "step":  return ["1", "2", "3", "4", "5", "6", "7", "8"];
    case "play":  return ["Off", "On"];
    default: return null;
    }
}
function optionName(key, raw) {
    const t = String(raw).trim(), list = optionsOf(key);
    if (!list || list.includes(t)) return t;
    const n = Number(t);
    return Number.isFinite(n) ? list[Math.max(0, Math.min(list.length - 1, Math.round(n)))] : t;
}
/* What else a picture reads, so a still one is drawn again when it changes. */
function depends(key) {
    if (key === "step" || key === "play") return ["seq_now"];
    const l = /^(m\d_)rate$/.exec(key);
    return l ? [l[1] + "kind"] : [];
}

const stills = new Map(), turned = new Map();
function drawCell(ctx, { values, group, nowMs, touched }) {
    const key = group && group.keys && group.keys[0];
    if (!key || !values) return;
    const role = roleOf(key), fn = D[role];
    if (!fn) return false;
    const raw = values[key];
    if (raw == null || String(raw).trim() === "") return;   /* no answer yet: no picture of a made-up value */
    const now = typeof nowMs === "number" ? nowMs : 0;
    const t = turned.get(key);
    if (!t || t.raw !== raw) turned.set(key, { raw, at: t ? now : -1e9 });
    const awake = !!(touched || (group && group.touched)) || now - turned.get(key).at < AWAKE_MS;
    const s = { param: key, opt: optionName(key, raw), num: Number(raw), values, now, awake };
    const moving = awake && (role === "rate" || role === "trail" || role === "mrate");
    const sig = String(raw) + "|" + depends(key).map((k) => values[k]).join("|");
    const hit = stills.get(key);
    if (!moving && hit && hit.sig === sig) { replay(ctx, hit.runs); return; }
    const p = pen();
    fn(p, s);
    const runs = p.runs();
    replay(ctx, runs);
    if (moving) stills.delete(key); else stills.set(key, { sig, runs });
}
function replay(ctx, runs) {
    const ox = Math.max(0, Math.floor(((ctx.width || W) - W) / 2)), oy = Math.max(0, Math.floor(((ctx.height || H) - H) / 2));
    for (let i = 0; i < runs.length; i += 3) ctx.fillRect(ox + runs[i], oy + runs[i + 1], runs[i + 2], 1, 1);
}

const overlay = { /* for tests */ _reset() { stills.clear(); turned.clear(); },
    widgetKinds: ["custom:viz"], drawCell, _D: D, _roleOf: roleOf, _optionsOf: optionsOf, _lists: { COLORS, RATES, SIZES, TRAILS, FOLDS, SHIFTS, REACTS, BARS, KINDS, MRATES, AIMS, SCENE_NAMES } };
globalThis.canvas_overlay = overlay;
})();
