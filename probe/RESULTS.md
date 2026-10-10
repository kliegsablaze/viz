# Probe results

Device runs of `viz-probe` with the user's Matrix Pro (Matrix OS **3.2**) on the Move's USB-A
port, Schwung **1.7.3**, 2026-10-10.

## Connection

- **The Matrix sometimes fails to connect when plugged in.** On the first try, neither
  direction worked: the Move played nothing from Matrix pads, and the probe's notes never
  lit anything. Unplugging and replugging fixed it. After that, Matrix pad presses play the
  Move's selected track (channel 1), and the probe's notes light pads.
- Schwung's note that nothing a multi-port device sends reaches the Move
  (`E16_REMOTE.md:113-127`) does **not** hold for the Matrix. Its notes arrive. SysEx is
  still untested (below).
- `midi_send_external` never refused a packet in any test (0 refusals, at most 8 packets
  per block).

## Layout — confirmed

- **Walk:** the pad note map in DESIGN.md is right: the corners, and the reading order from
  the top left.
- **Edge:** the underglow map is right: right 100–107, left 108–115, bottom 116–123,
  top 28–35, with the side order as designed.

## Power — the Move's USB-A port can't light the whole Matrix brightly

The symptoms were pads going dark at random, then the Matrix locking up completely (Fn
dead) until it was replugged. It happened with heavy colour on every light, not with fast
messages: at 400 messages/s, evenly spaced, it still froze.

| Test (all 96 lights unless noted) | Matrix brightness | Result |
|---|---|---|
| Palette 1 with a white underglow | default | froze after 3 switches (twice) |
| Palette 1 with a white underglow, 400/s | minimum | froze after the 5th switch |
| White Edge (underglow only), grey 1→7 | minimum | never froze |
| White Pads (pads only), grey 1→7 | minimum | never froze |
| Palette 1, underglow off | minimum | never froze |
| White All, grey 1→7 | minimum | never froze |
| White All, grey 1→7 | **maximum** | **froze at LEVEL 4** (palette 124 = `#919191`); LEVEL 3 (123 = `#6D6D6D`) was safe |

**Consequence for Viz:** a power guard. Each frame, Viz estimates the draw as the sum of
R+G+B over all lit lights (palette RGB, 0–255 each). If the estimate exceeds the budget, it
dims the whole frame.

The budget is calibrated for the **worst case, the Matrix at maximum brightness**: all 96
lights at `#6D6D6D` is safe, and at `#919191` freezes. So the 8-bit sum budget is ≈ 96 × 3 ×
109 ≈ 31,400, with a margin. A powered USB hub between the Move and the Matrix should lift
the limit; that's an untested setting.

## Still to run

Flood (throughput at SPEED Max), SysEx RGB, SysEx Palette, Beat (sync and NUDGE),
Move→Schwung on/off, and the render bench. Bench so far: a 32x32 domain-warped fbm field is
0.44–0.47 ms per frame, and a 16x16 fluid step is 0.07 ms, on the worker thread. Far
under budget.
