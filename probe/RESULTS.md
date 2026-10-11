# Probe results

Device runs of `viz-probe` with the user's Matrix Pro (Matrix OS **3.2**) on the Move's USB-A
port, Schwung **1.7.3**, 2026-10-10.

## Connection

- **The Matrix sometimes fails to connect when plugged in.** On the first try, neither
  direction worked: the Move played nothing from Matrix pads, and the probe's notes never
  lit anything. Unplugging and replugging fixed it. After that, Matrix pad presses play the
  Move's selected track (channel 1), and the probe's notes light pads.
- Schwung's note that nothing a multi-port device sends reaches the Move
  (`E16_REMOTE.md:113-127`) does **not** hold for the Matrix. Its notes arrive at the
  Move, and SysEx from the Move reaches it (below).
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
| White Pads, grey 1→7 | **maximum** | **froze at LEVEL 6** (126 = `#DADADA`); LEVEL 5 (125 = `#B6B6B6`) was safe |
| White Edge, grey 1→7 | **maximum** | **froze at LEVEL 7** (127 = `#FFFFFF`); LEVEL 6 (`#DADADA`) was safe |

**Consequence for Viz:** a power guard. Each frame, Viz estimates the draw as the sum of
R+G+B (0–255 each) over the lit lights, with each **underglow light counted twice**. If the
estimate exceeds the budget, it dims the whole frame.

Working it out, in pad-units (one pad's R+G+B at maximum Matrix brightness), with *w* the
draw of one underglow light relative to one pad and *T* the freeze point:
- pads alone: 64 × 546 = 34,944 safe, 64 × 654 = 41,856 froze;
- underglow alone: 32 × 654 × *w* safe, 32 × 765 × *w* froze;
- all 96: 64 × 327 + 32 × 327 × *w* safe, 64 × 435 + 32 × 435 × *w* froze.

Only *w* between ≈ 1.49 and 2.0 fits all six, with *T* above ≈ 36,550. So counting the
underglow ×2 is the cautious end, and a **budget of 33,000** leaves a 10% margin. That allows
pads alone up to `#ABABAB`, and all 96 lights up to `#565656`. It holds at any Matrix
brightness setting. A powered USB hub between the Move and the Matrix should lift the limit;
that's an untested setting.

## Throughput — not a limit

Flood: all 96 lights retargeted every block, at the Matrix's minimum brightness.

| SPEED | What the user saw |
|---|---|
| 1000/s | some pads looked like they never changed colour |
| 2000/s | every pad changing; looked right |
| Max (≤ 20 packets/block ≈ 6,900/s) | much faster flicker; some pads looked solid |

**No freezes at any rate.** The "stuck" pads at 1000/s and Max are aliasing, not loss. Flood
changes the target hue every block, and each pad is refreshed on a fixed round-robin period;
when that period is a multiple of the 8-hue cycle, the pad gets the same hue every time.
Every freeze seen earlier was power (above). Viz targets **≈ 2,000 packets/s**, to be
confirmed with real scenes.

## SysEx — both paths work (contradicts the design's assumption)

| Test | Result |
|---|---|
| SysEx RGB (`F0 00 02 03 4D 58 5E (idx r g b)… F7`, 88 packets) | **rainbow shown**, re-sent every 2 s |
| SysEx Palette (`… 41 3D 00 (idx r g b)… F7`, 174 packets, then notes on ch 3) | **gradient shown** (black → ember → white) |

So the Move's USB-A port **does** carry SysEx to the Matrix, despite its two USB-MIDI ports.
That opens up:
- **Custom gradients:** one 128-step gradient per scene, uploaded to RAM (never `41 7D`, so
  no flash write), then one note per pad, for smooth colour.
- **Direct RGB** (`5E`): any 18-bit colour per pad, at ≈ 1.33 packets per pad.

The built-in palettes are now the fallback.

## Clock sync — works

Beat test at 120 BPM: 735 beats counted from `get_beat_position()`, and the red downbeat
landed on beat 1 (time signature 4/4 from `move_info`).

**NUDGE −40 ms** looked right to the user: lights driven straight off the clock are about
40 ms ahead of the sound heard. Viz's default NUDGE is −40.

At 2,000 packets/s, some pads stayed dark on each whole-grid flash. A flash changes all 96
lights, which takes ~48 ms at that rate, while the flash lasts ~87 ms in three steps; pads
late in the queue miss it. **Whole-grid changes go as SysEx `5E` index 0 (fill all), about 4
packets**, instead of 96 notes.

**Beat Fill** (the same flash, each step one `5E` index-0 message) at the Matrix's maximum
brightness: **every pad and the underglow lit together, none dark.** The white step peaks at
6-bit 26 (`#6B6B6B`), inside the power limit.

## What a Master FX hears — Schwung's modules always; Move's own only with Move→Schwung

| Time | Move→Schwung | Playing | Probe input |
|---|---|---|---|
| 23:34:32 | switched **OFF** | — | — |
| 23:34:37–23:35:40 | off | a drum clip played by **Strut** (a Schwung slot) | **−12 to −15 dBFS, continuous** |
| 23:35:44 → | off | stopped | silence |
| 23:36:25 | switched on | stopped | — |

**Correction (the user, 2026-10-10):** the drums were Strut in a Schwung slot, not one of
Move's own instruments. An earlier version of this file read the run as "Master FX hears
Move's tracks without Move→Schwung"; that does not follow. The run shows only that **a
Master FX hears Schwung's own slot modules**, with Move→Schwung off, which is what Schwung's
notes say (CLAUDE.md "Gain Staging").

Earlier, with Move→Schwung on and **only the metronome** playing, the probe heard silence,
broken by bursts (probably hand-played pads). That is correct: Schwung adds its metronome
after Master FX.

**Move's own instruments** (a clip on a Move instrument, everything else muted, TEST Off):

| Time | Move→Schwung | Playing | Probe input |
|---|---|---|---|
| 23:56:18 | switched **off** | — | — |
| 23:56:27–23:56:49 | off | yes (beats 2.9–47) | **silence** |
| 23:57:04 | switched **on** | — | — |
| 23:57:09–23:57:37 | on | yes (beats 3.7–60) | **−20.5 dBFS, continuous** |

So Schwung's notes are right: a Master FX hears Move's own instruments **only with
Move→Schwung on**. Viz's help says to turn it on.

## Nothing comes back from the USB-A device

Through the whole session the probe's count of MIDI arriving from external sources stayed at
**0**, including a deliberate 10 s of Matrix pad presses (which played the Move's drums).
A Master FX can't hear the device, so Viz can't detect what's plugged in, and can't ask it
anything (no identity request). Any choice of device has to be a setting.

## Move's own MIDI out paints the Matrix

With the Strut drum clip playing, the bottom-left pads (notes 36–38) lit purple in time with
the drums. The clip is sequenced on a Move track, and Move sends that track's notes out of
USB-A even though Strut makes the sound; the Performance app paints them.
Consequences:
- Viz must **repaint lights it didn't set**: a periodic refresh that marks every light dirty,
  not just one row.
- help.json should say to turn off Move's MIDI output to USB-A for a clean picture.
- Later idea: those notes say exactly what's playing, and could feed the scenes. They can't
  reach a Master FX today, so this is out of scope.

## Render bench

A 32x32 domain-warped fbm field is
0.44–0.49 ms per frame, and a 16x16 fluid step is 0.07 ms, on the worker thread. Far
under budget.
