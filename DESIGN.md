# Viz

*Viz turns a Matrix into a light show for whatever the Move is playing: bold, geometric shapes
in clear colours, stepping on the beat, with eight knobs you can play like an instrument.*

**Status:** 0.1.0 built and installed. On 2026-10-10 the user turned the show from organic
pictures to geometric scenes (see *The aesthetic brief*); the 100 scenes chosen in the scene
gallery are ported to C with exact parity and passed on the Matrix. On 2026-10-11 the
Sequence and Mod pages (now SEQ and MOD, before Settings) and the knob pictures were added,
and Viz now replaces the notes Move sends to the Matrix (passed on the device); the pages'
device pass is next.
Written against Schwung **v1.7.3** and Matrix OS commit **e1a4c08** (2026-09-25); the user's
Matrix runs Matrix OS **3.2**.

The facts about the Matrix were read from its source, and the facts about the host from
Schwung's source. The probe (`probe/`, results in [probe/RESULTS.md](probe/RESULTS.md)) then
checked them on the user's Move and Matrix. **Two of the design's assumptions turned out
wrong, and the design now follows the measurements:**
- full-colour SysEx **does** reach the Matrix;
- the Move's USB-A port **cannot** power the whole Matrix brightly.

- **Module ID:** `viz`
- **Component type:** `audio_fx`, loaded in the **Master FX** row
- **API:** plugin API v2 (`audio_fx_api_v2`), pure C, `ui_hierarchy` in `module.json`, no
  JavaScript UI
- **Versions:** start at 0.1.0. The whole design below working smoothly is **0.5.0**.

---

## What it is

You plug a 203 Systems **Matrix** (the Kickstarter Matrix Pro, now sold as the Mystrix) into
the Move's **USB-A** port and open its **Performance** app. Then you load Viz into a Master
FX position. From then on the Matrix's 8x8 pads and its 32 underglow LEDs become a light
show for the music:

- **Move playing:** the shapes step on Move's clock: a ring bursts on every beat, a snake
  takes one pad per 16th, the colours swap on the bar.
- **Move stopped:** the same scenes follow the sound coming out of the master. Kicks make
  the beat, and the hits and bands drive the scenes that read them.
- **The knobs perform.** Each Look knob has a change you can see at once: faster steps,
  bigger shapes, longer trails, a mirror fold, a colour swap, a kick that flashes the grid.

Viz leaves the audio untouched. It is an "effect" only in where it sits.

### The aesthetic brief

The first build led with organic scenes (auroras, ink, fireflies). On the device the user
found them random-looking (2026-10-10):

> *"A lot of visual performances with grid controllers like these rely on more geometric
> animations, regular shapes and clearer colour changes."*

They also asked for knobs with a more tangible effect, "so it feels more performative", and
kept organic scenes as a side dish: "nice to have", but the main set must be "visually
distinct, clear to the eye".

So the main set follows what grid-controller light shows do (Launchpad light-show
performers, Apollo Studio projects, lighting desks):
- the grid is an **8x8 screen**: solid colours on black, every pad either lit or dark;
- **hard-edged shapes** (rings, bars, sprites, sweeps) that **step on the beat**;
- **mirroring** for symmetry, and **bursts** on hits;
- from lighting desks: a speed control, a few **colour sets** of three colours each, and
  strobe-like effects used sparingly.

The scenes were drawn, judged and picked in a scene gallery on the Mac (*Scenes*), over six
rounds, until the user had 100 they wanted. The six organic scenes stay, after the 100.

---

## Lineage

**Grid light shows**
- Launchpad light-show performers, who play a song's lights as pads, and mat1jaczyyy's
  **Apollo Studio**, whose SysEx Viz speaks. Their grammar is the main set's: solid colour
  on black, hard shapes, steps on the beat, mirrored and burst.
- Lighting desks: a speed fader, colour palettes, chases and strobe. RATE, COLOR and REACT
  come from there.

**Light organs and visual music**
- Alexander Scriabin, *Prometheus: The Poem of Fire* (1910). Its score includes a part for
  the *clavier à lumières*, a keyboard of coloured light.
- Mary Hallock-Greenewalt's light-organ patents (1919 onwards), including the rheostat
  that made light fade instead of switch.
- Thomas Wilfred's **Lumia** (1920s onwards): slow, abstract, ever-changing light
  compositions. This is the closest ancestor of Viz's Journey.
- Oskar Fischinger's abstract films. Jordan Belson and Henry Jacobs's *Vortex* concerts
  (1957–59).
- John Whitney, *Permutations* (1968) and *Arabesque* (1975): harmonic motion made visible.

**Music visualizers**
- Atari Video Music (1976), the first consumer music visualizer.
- Ryan Geiss's **MilkDrop** (2001): feedback, warping, and presets that blend into each
  other.
- Brian Eno, *77 Million Paintings* (2006): generative and never-repeating, shown in slow
  cross-dissolves. Viz's dissolve and gene drift take after it.

**Launchpad light shows**
- mat1jaczyyy's **Apollo Studio** and the Launchpad Pro custom firmware. Its SysEx
  batch-fill format is the one Matrix OS implements and that Viz would use for full colour.

**The algorithms behind the scenes**
- Ken Perlin, "An Image Synthesizer" (SIGGRAPH 1985): gradient noise.
- Inigo Quilez's writing on domain warping and cosine palettes.
- Robert Bridson, Jim Houriham and Marcus Nordenstam, "Curl-Noise for Procedural Fluid
  Flow" (SIGGRAPH 2007).
- Jos Stam, "Stable Fluids" (SIGGRAPH 1999).
- John E. Pearson, "Complex Patterns in a Simple System" (*Science* 1993): Gray-Scott
  reaction-diffusion.
- Jeff Jones, "Characteristics of Pattern Formation and Evolution in Approximations of
  Physarum Transport Networks" (*Artificial Life* 2010).
- Bert Wang-Chak Chan, "Lenia: Biology of Artificial Life" (*Complex Systems* 2019).
- Craig Reynolds, "Flocks, Herds, and Schools" (SIGGRAPH 1987): boids.
- Yoshiki Kuramoto's coupled oscillators (1975). Steven Strogatz, *Sync* (2003): fireflies
  that flash together.
- Helmut Vogel, "A better way to construct the sunflower head" (*Mathematical Biosciences*
  1979): phyllotaxis.
- John Conway's Game of Life, as published by Martin Gardner (*Scientific American*, 1970).

**Listening**
- Juan Pablo Bello et al., "A Tutorial on Onset Detection in Music Signals" (*IEEE Trans.
  Speech and Audio Processing*, 2005). Spectral flux per band group is Viz's hit detector.

---

## How the Matrix is driven

Everything in this section was read from Matrix OS (`github.com/203-Systems/MatrixOS`,
commit e1a4c08). The Matrix Pro is the `Mystrix1` device family, variants V100/V110
(`Devices/Mystrix1/Variants/Mystrix/V1x0/Config.h:60`, "Mystrix Pro V1x0 Config Loaded").

### The Performance app paints pads from MIDI notes

`Applications/Performance/8x8/Performance8x8.cpp`, `NoteHandler` (:158):

- **Note On** (and CC) on channel *c* with velocity *v* sets that pad to `palette[c][v]`.
- **Velocity 0 or Note Off** makes the pad dark.
- **Channel 1:** the Matrix palette, `Performance8x8.h:93-224`. It has 24 hues × 5 shades
  (dim `3C`, `7D`, `BE`, full `FF`, and a pastel) in indices 1–120, then 7 greys and white
  in 121–127.
- **Channel 2:** the Launchpad X palette (`:225-355`). It is a different 128 colours, many of
  them not in the Matrix palette.
- **Channels 3–6:** four custom palettes, uploadable by SysEx (see below).

Viz's main path is full RGB by SysEx (below). Its **note fallback** uses both channel 1 and
channel 2: the union of the two palettes is about 250 distinct colours, nearly double the
shades available to a gradient, at no cost, because the channel nibble is free.

### Which note is which pad

The note map is Ableton's drum-rack layout, `user1_keymap_optimized` (`Performance8x8.h:88`,
read through `NoteToXY`, `.cpp:77`). Take x = 0–7 from left to right and y = 0–7 from top to
bottom:

```
x < 4 :  note = 64 - 4*y + x
x >= 4:  note = 96 - 4*y + (x - 4)
```

So the bottom-left pad is note 36 and the top-right pad is note 99.

The 32 **underglow** LEDs (`MatrixOSConfig.h`, partition "Underglow", 32 LEDs) are 8 per
side:

| Side   | Notes   |
|--------|---------|
| Right  | 100–107 |
| Left   | 108–115 |
| Bottom | 116–123 |
| Top    | 28–35   |

### Full colour: SysEx, and it reaches the Matrix

The Performance app also understands three SysEx messages. Each one starts with the header
`F0 00 02 03 4D 58` (`OS/MIDI/MIDI.h:8-9`), which `ProcessSysEx` (`OS/MIDI/MIDI.cpp`) hands
on to the app:

- **`5E` (index r g b)…**: sets each pad in the list to its own 6-bit RGB colour. The index
  is Launchpad-Pro style (11–88, x = idx % 10 − 1, y = 8 − idx / 10); index 0 fills the
  whole grid (`.cpp:203-233`, `:338-361`).
- **`5F`**: one colour to many pads (`:286-337`).
- **`41 3D <p> (idx r g b)…`**: writes custom palette *p* (channels 3–6) into RAM
  (`:375-404`). Only `41 7D` saves it to flash (`:405-416`). **Viz never sends `41 7D`**,
  so there is no flash wear, and a power cycle restores the user's own palettes.

The Apollo index also reaches the underglow. `ApplyApolloIndex` maps idx 1–8 and 91–98 to
the bottom and top rows outside the grid, and idx 10/20…80 and 19/29…89 to the left and
right columns (`.cpp:203-233`). So one message format covers all 96 lights.

The Matrix is a composite USB device (MIDI + serial + HID) with **two MIDI ports**
(`OS/USB/TinyUSB/usb_desc_default.cpp:106-149`). Schwung had measured that Move's USB-A
cannot exchange SysEx with a multi-port device (`schwung/docs/E16_REMOTE.md:113-127`). **The
Matrix is an exception, measured:** both `5E` (a rainbow) and `41 3D` (a custom gradient)
took effect, and the Matrix's own notes reach the Move (probe Tests 7–8, RESULTS.md).

So **full colour is Viz's main path** (see *The light engine*). Plain notes on the built-in
palettes are the fallback.

### Other Matrix behaviour that matters

- The app drains at most 64 MIDI packets per loop (`Performance8x8.h:44`), and its LEDs
  refresh at 120 fps (`Devices/Mystrix1/MatrixOSConfig.h:28`). The probe ran it at up to
  ~6,900 packets/s with no freeze and no loss.
- **Flicker reduction** ("stfu") delays a note-off by one LED frame (`.cpp:131-148`). This is
  harmless for Viz.
- Pressing a Matrix pad **sends a note** (`.cpp:108`), and the Fn key sends CC 121/123
  (`:121-127`). **These reach the Move and play its selected track** (channel 1, measured).
  Master FX don't receive USB-A input (`shadow_midi.c:1040-1169`), so Viz can't use or
  swallow them. help.json says that touching the Matrix plays the Move.
- **Move's own MIDI out paints the Matrix.** With a drum clip playing (sequenced on a Move
  track, sounded by Strut in a Schwung slot), the bottom-left pads (notes 36–38) lit up in
  time with the drums: Move sends its track notes out of USB-A.
  Viz **covers each of those notes as it leaves** (see *Move's own notes*), so nothing needs
  changing on the Move.
- **Connecting can fail silently.** On the first plug-in, neither direction worked. A replug
  fixed it. There is no hotplug signal on USB-A, so Viz can't detect it; help.json says
  "if the Matrix stays dark, replug it".
- **Power is the real limit.** The Matrix asks for 500 mA (`usb_desc_default.cpp:146`), and
  the Move's USB-A port can't supply what a brightly lit Matrix draws. Too much light makes
  pads drop out, then the Matrix locks up (Fn dead) until it is replugged. Measured:
  - at the Matrix's **minimum** brightness, all 96 lights at full white is safe;
  - at **maximum** brightness, all 96 at `#6D6D6D` is safe and at `#919191` it freezes.

  Viz has a **power guard** (see *The light engine*) calibrated to the worst case, so it is
  safe whatever the Matrix's brightness. A powered USB hub lifts the limit (POWER setting).

### Setting up the Matrix

1. Connect the Matrix's USB-C to the Move's USB-A, directly or through a powered USB hub.
2. On the Matrix, open **Performance** (the red app). If nothing responds later, replug.
3. Optional: turn on its **Menu Lock**, so a stray Fn press doesn't open the menu over the
   show.
4. Leave Master FX **MIDI Ch** on **All** (the default), so Viz hears every note Move
   sends to the Matrix and can paint over it (*Move's own notes*).
5. To light up to Move's own instruments (not only Schwung modules), turn on Move→Schwung in
   Schwung's Global Settings.
6. Load Viz into a Master FX position.

---

## Other grids: the device layer

The user, 2026-10-10: Viz should be useful to anyone with a grid, not only a Matrix owner,
and Launchpads are the obvious case. They have no Launchpad, so **Launchpad support ships
untested** and is labelled so in the help, until someone runs it.

**What differs between grids is only the last step.** Scenes draw an 8x8 picture plus an
**edge**: 32 positions, 8 per side, in the order right (top to bottom), left (top to bottom),
bottom (left to right), top (left to right). A **device** turns that into bytes:

| | Matrix | Launchpad Mini MK3 / X | Launchpad Pro MK3 | Launchpad MK2 | Launchpad Pro (2015) |
|---|---|---|---|---|---|
| Header | `F0 00 02 03 4D 58` | `F0 00 20 29 02 0D` / `0C` | `F0 00 20 29 02 0E` | `F0 00 20 29 02 18` | `F0 00 20 29 02 10` |
| Colour message | `5E (idx r g b)…` | `03 (03 idx r g b)…` | `03 (03 idx r g b)…` | `0B (idx r g b)…` | `0B (idx r g b)…` |
| Bits per channel | 6 | 7 | 7 | 6 | 6 |
| Bytes per light | 4 | 5 | 5 | 4 | 4 |
| Pads | idx 11–88 | 11–88 | 11–88 | 11–88 | 11–88 |
| Edge | all 32 (underglow) | right 19–89, top 91–98 | all 32: left 10–80, right 19–89, bottom 101–108, top 91–98 | right 19–89, top 104–111 | all 32: left 10–80, right 19–89, bottom 1–8, top 91–98 |
| Set up on load | nothing | Programmer mode `0E 01` | Programmer mode `0E 01` | Session layout `22 00` | Programmer layout `2C 03` |
| Fill everything | `5E 00 r g b` | none | none | none | none |
| Measured | **yes** (probe) | no | no | no | no |

The Launchpad columns come from Novation's programmer's reference guides, not from a
device. The pad index scheme (row × 10 + column, from the bottom left) is the same on every
grid, which is why one picture serves them all.

- **The user picks the grid (GRID, on Settings).** Viz can't detect it: a Master FX
  receives nothing from the USB-A device (probe, "Nothing comes back"), so there is no
  identity request.
- **Set up, and set up again.** The mode message goes out when Viz loads, and again every
  2 s with the refresh, so a replugged Launchpad comes back. Removing Viz can't send the
  exit message (see *All-off*).
- **Flashes on a grid without fill-all** stream like any other frame, biggest change first.
  A whole-grid flash on a Mini MK3 costs ≈ 110 packets, so it lands over ≈ 55 ms. Using the
  Launchpad's global brightness message (`08`) for flashes is an idea for after a Launchpad
  has been tested.
- **Power.** Every grid gets the same guard and budget, with its edge lights counted once
  (only the Matrix's underglow was measured to draw more). That is a guess for Launchpads,
  and POWER Hub turns it off.
- **Unknown until tested:** which of a Launchpad's two or three USB-MIDI ports the Move
  reaches (the Matrix also has two, and works); whether its SysEx survives the trip
  (Schwung found it did not for an OXI E16, `E16_REMOTE.md:113-127`); and how much power it
  draws.

---

## Where Viz sits, and what it hears

**Master FX**, chosen by the user over a Tool on 2026-10-10.

A Master FX is an `audio_fx_api_v2` plugin. `process_block(inst, int16_t *audio_inout,
frames)` processes in place, stereo interleaved int16, 128 frames at 44.1 kHz
(`src/host/audio_fx_api_v2.h:19-32`; `plugin_api_v1.h:119-120`). Viz reads the buffer and
never writes it.

**What a Master FX hears.** Schwung's notes describe the Master FX buffer as Schwung's own
audio (its slot modules), with Move's own tracks added afterwards unless Move→Schwung (Link
Audio rebuild) is on (CLAUDE.md "Gain Staging"; `schwung_shim.c:3570-3580`, `:3745-3747`,
`:2994-2997`). **The probe confirms it** (RESULTS.md, "What a Master FX hears"):
- a drum clip played by Strut in a Schwung slot reached the probe at −12 to −15 dBFS with
  Move→Schwung **off**;
- a clip on one of Move's own instruments was **silent** to the probe with Move→Schwung off,
  and arrived at −20.5 dBFS with it **on**.

So:
- **Schwung modules** always reach Viz, with no setting.
- **Move's own instruments** reach Viz only with **Move→Schwung on** (measured). help.json
  and *Setting up the Matrix* say to turn it on.
- **The metronome is invisible to Viz:** Schwung adds its click after Master FX. That is
  correct; nobody wants the show to pulse to the click.
- With Move playing, the Beat pictures need no audio at all: the clock drives them whatever
  Viz hears.

**Rejected: a Tool.** Loom proved that a Tool with `capabilities.end_of_chain` hears the
whole final mix and keeps running after Back. It would hear Move's own instruments with no
setting. The user wanted Viz to live where effects live; the cost is that Move→Schwung must
be on for Viz to hear Move's own instruments, and the measurement shows that this works.

---

## What Viz listens to

Every scene reads one shared set of inputs. That is what makes **every scene work in every
mode**: no scene is "sync only" or "EQ only".

### Time

- **Beat phase and bar phase.** These come from `host->get_beat_position()`
  (`shadow_transport_beat_position`, `src/host/shadow_transport.c:135-147`). It counts
  beats from Move's 24-PPQN clock, interpolated per block, and returns < 0 when stopped.
  It is wired for Master FX (`shadow_chain_mgmt.c:2870-2879`).
- **"Is Move playing?"** is `get_beat_position() >= 0`. `get_clock_status` is left NULL for
  Master FX, and `move_info.playing` reads 255 on unpinned Move builds (Loom's finding,
  `move_info.c:64-80`).
- **Tempo** comes from `get_bpm()`.
- **Bar length** comes from `move_info` `ts_upper`/`ts_lower`, read with `move_info_read()`.
  The first call is in `create_instance` (`move_info.h:16-20`). The fallback is 4/4.
- **Phrases** are CYCLE bars long, counted from the transport start.
- **When stopped, Viz makes its own beat** from the low-band hits. A hit resets the beat
  phase toward 0 with a soft pull, so a pulsing kick still gives a breathing picture.
  There is no audio tempo detection in 0.x.

### Sound

The mix is summed to mono (keeping stereo width as one extra number), then:

- **8 bands.** Bandpass biquads on octave centres from 60 Hz to 8 kHz, each followed by an
  attack/release envelope.
- **Loudness** and **brightness** (spectral tilt, high bands over low).
- **Three hit pulses:** low (kick), mid (snare and clap), high (hats). Each is
  half-wave-rectified flux on its band group, with an adaptive threshold and a short
  refractory time.
- **Automatic level.** A slow AGC (rising over ~2 s, falling over ~20 s) normalises all of
  the above. A quiet ambient pad and a loud techno track both fill the range, so **there is
  no gain knob.**

### Long time

- **Energy arc:** loudness and hit density smoothed over ~30 s, from 0 (sparse, calm) to 1
  (dense, driving).
- **Section change:** fires when the last ~2 s of band levels differ sharply from the last
  ~30 s. This catches a breakdown, a drop, or a new part.

### Mode

`MODE` picks where time and sound come from:

| Mode  | Move playing                   | Move stopped                        |
|-------|--------------------------------|-------------------------------------|
| Auto  | clock time + sound             | sound time (own beat) + sound       |
| Beat  | clock time; sound ignored      | idles: slow dreaming, no reactivity |
| Sound | sound time; clock ignored      | sound time + sound                  |
| Blend | clock time + sound (stronger)  | as Auto                             |

- **Auto** is the user's rule, and the default.
- **Blend** differs from Auto when playing: the clock moves the picture and the sound sets
  its *size and brightness* at twice Auto's depth.

### How music touches a scene

The geometric scenes keep the music legible by keeping it simple:

| Musical input  | What the scenes do with it                                  |
|----------------|-------------------------------------------------------------|
| Beat           | **steps**: RATE turns beats into steps (1 bar to 1/16), and most scenes move one step at a time, with a smooth phase inside the step for sprites in flight |
| Bar            | SHIFT Bar swaps the three colours                           |
| Kick           | REACT (Glow, Grow, Flash, Flip); SHIFT Kick; some scenes count or burst on it |
| Snare, hats    | a few scenes read them directly (Kit, Beats, Tally…)        |
| 8 bands, bass  | the meter scenes (Bars, Bass, Speaker…)                     |
| Hit log        | the last four bars of kicks, snares and hats, a 16th per slot, for Steps-style scenes |

---

## Scenes

**100 geometric scenes, then six soft ones.** SCENE lists them in this order, 106 in all.

### How they were chosen: the scene gallery

`tools/gallery/gallery.html` is a single page that runs on the Mac. It plays a drum loop
(WebAudio: eight bars with a fill in bar 3 and a breakdown in bars 6–7) and shows every
scene as a live 8x8 tile with the real knobs. The user marked each scene Keep, Maybe or
Drop, and replacements were drawn for every drop. It took six rounds, from about 20 scenes to
the final 100 (97 kept, plus UFO, Stairs and Tally as Maybes).

**The gallery is the reference.** `src/dsp/shapes.c` is a line-for-line port of its
JavaScript, and a test runs both on the same loop and compares every pad of every frame
(*Testing*). So a scene looks on the Matrix the way it looked when the user picked it. Change
a scene in the gallery first, then port the change.

### The 100

"Uses" names the knobs that change a scene's shape beyond the ones every scene has (COLOR,
TRAIL, FOLD, SHIFT and REACT work on all of them).

| # | Scene | The picture | Uses |
|---|-------|-------------|------|
| 1 | **Pulse** | The whole grid (or a centre square at smaller SIZE) flashes on every step and fades. | RATE SIZE |
| 2 | **Rings** | A square ring bursts out from the centre on every step. | RATE SIZE |
| 3 | **Diamond** | A diamond ripples out from the centre on every step. | RATE SIZE |
| 4 | **Burst** | Each kick and snare sets off a ring from a random pad. The kick's rings come from near the centre. | SIZE |
| 5 | **Sweep** | A bar sweeps back and forth, one pad per step. It turns sideways every other pass. | RATE SIZE |
| 6 | **Cross** | A plus sign and an X take turns, one per step. | RATE SIZE |
| 7 | **Snake** | A snake spirals from the edge to the centre and back, two pads per step. SIZE sets its length. | RATE SIZE |
| 8 | **Radar** | A hand sweeps round the centre, one turn every four steps. SIZE widens it. | RATE SIZE |
| 9 | **Stripes** | Diagonal stripes slide across, one pad per step, like a barber's pole. | RATE SIZE |
| 10 | **Tunnel** | Nested squares zoom outward, one ring per step. SIZE spaces the rings. | RATE SIZE |
| 11 | **Quads** | The four quarters light in turn, clockwise, one per step. Small SIZE lights just the middle of each quarter. | RATE SIZE |
| 12 | **Shapes** | A different shape on every step: square, diamond, X, plus, ring, block. | RATE SIZE |
| 13 | **Steps** | The 16 steps of the bar as 16 blocks. The current step is bright; kicks leave a mark. | (follows the bar) |
| 14 | **Rise** | Rows fill from the bottom, SIZE rows per step, then the full grid flashes and it starts again. | RATE SIZE |
| 15 | **Bounce** | A ball bounces across the grid and lands on every step. | RATE SIZE |
| 16 | **Wave** | A sine wave rolls across. The bass makes it taller. | RATE SIZE |
| 17 | **Rain** | Drops fall down the columns. More drops on every hi-hat; SIZE sets how many. | RATE SIZE |
| 18 | **Bars** | The classic equalizer: eight frequency bands as columns. SIZE makes fewer, wider bars. | SIZE |
| 19 | **Spiral** | Two spiral arms turn round the centre, a quarter turn per step. SIZE thickens the arms. | RATE SIZE |
| 20 | **Pinwheel** | Coloured blades click round one notch per step. SIZE sets how many blades. | RATE SIZE |
| 21 | **Kit** | Each drum has its own shape: the kick a centre square, the snare the four corners, the hi-hat the outer ring. | SIZE |
| 22 | **Icons** | A picture on every step: heart, smiley, invader, note, star. | RATE |
| 23 | **Count** | Counts the steps out loud: big numbers 1 to 8. | RATE |
| 24 | **Arrow** | A big arrow flies across the grid. It points a new way (up, right, down, left) on every step. | RATE |
| 25 | **Corners** | Light runs in from all four corners and meets in the middle on every step. | RATE SIZE |
| 26 | **Stack** | Blocks drop and stack up like Tetris, one per step. Full rows clear; when it reaches the top, the grid flashes and empties. | RATE SIZE |
| 27 | **Rocket** | A rocket shoots up and bursts into a ring, once per step. SIZE makes the burst bigger. | RATE SIZE |
| 28 | **Halves** | The grid is cut into SIZE + 1 bands that light one after another. The cut turns sideways each round. | RATE SIZE |
| 29 | **Pong** | A game of Pong: the ball moves four pads per step and the paddles chase it. SIZE sets the paddle length. | RATE SIZE |
| 30 | **Trace** | A heart monitor: a line scrolls across and spikes on every kick. | RATE |
| 31 | **Invaders** | Space invaders march side to side and creep down a row every four steps. Your cannon at the bottom fires on every kick. | RATE |
| 32 | **Breakout** | The ball knocks out bricks, two moves per step. When the wall is gone it builds again. SIZE sets the paddle width. | RATE SIZE |
| 33 | **Text** | The word VIZ scrolls across, two columns per step. (The word could be anything you like.) | RATE |
| 34 | **Spin** | A square outline turns smoothly, an eighth of a turn per step, and changes colour every quarter turn. SIZE sets how big it is. | RATE SIZE |
| 35 | **Heart** | A heart that beats: big at the start of every step, then small. | RATE |
| 36 | **Sand** | An hourglass: sand runs from the top half to the bottom over 16 steps, then the glass turns over. | RATE |
| 37 | **Chomp** | A chomping mouth crosses the grid eating a row of dots. Its mouth snaps shut on every step. | RATE |
| 38 | **Doors** | Two doors slam shut at the start of every step, then slide open. They swap between side-to-side and up-and-down. | RATE |
| 39 | **Flap** | A bird flaps up on every kick and sinks in between, while pipes scroll past two columns per step. | RATE |
| 40 | **Jump** | A runner jumps on every kick while cactuses scroll towards it along the ground. | RATE |
| 41 | **Noughts** | Noughts and crosses: one move per step, the two players in two colours, then a new game. | RATE |
| 42 | **Four** | Connect Four: a disc drops into a column on every step, the colours taking turns. When the board is full it clears. | RATE |
| 43 | **Road** | Driving down a winding road: the edges sweep past four rows per step and the car stays in its lane. | RATE |
| 44 | **Maze** | A dot runs through a maze, two pads per step, and turns back at the end. | RATE |
| 45 | **Dice** | A die rolls on every step: it tumbles for a moment, then lands on a number. | RATE |
| 46 | **Catch** | A dot falls on every step and the basket slides across to catch it. | RATE |
| 47 | **Lander** | A lander drops toward the pad a row per step, firing its engine on the kick, and lands every eight steps. | RATE |
| 48 | **Tanks** | Two tanks trade shots: the left one fires on the kick, the right one on the snare. | - |
| 49 | **Face** | A face that changes mood on every step: happy, sad, winking, surprised. | RATE |
| 50 | **Eye** | An eye looks left, ahead and right, one glance per step, and blinks on every snare. | RATE |
| 51 | **Sun** | A sun whose rays switch between straight and diagonal on every step. | RATE |
| 52 | **Moon** | The moon goes from new to full and back over 16 steps. | RATE |
| 53 | **Flower** | A flower opens its petals on every step, four petals then eight. | RATE |
| 54 | **Tick** | A tick and a cross take turns, each drawn stroke by stroke during its step. | RATE |
| 55 | **Speaker** | A loudspeaker sends out sound waves on every kick. | - |
| 56 | **Saturn** | A planet with a ring that tilts round a notch per step. | RATE |
| 57 | **Clock** | A clock: the long hand moves an eighth of a turn per step, the short hand once per turn of the long one. | RATE |
| 58 | **Charge** | A battery charges one bar per step, flashes when full, and starts again. | RATE |
| 59 | **UFO** | A flying saucer drifts side to side, a pad per step, and shines its beam down on every kick. | RATE |
| 60 | **Wipe** | A new colour wipes over the old one on every step, coming from a different side each time. | RATE |
| 61 | **Curtain** | A new colour drops down column by column, left to right, on every step. | RATE |
| 62 | **Iris** | A circle of new colour opens from the centre on every step. | RATE |
| 63 | **Pie** | A new colour sweeps round like a clock hand, filling the grid once per step. | RATE |
| 64 | **Diag** | A new colour wipes in diagonally from a corner, a different corner on every step. | RATE |
| 65 | **Zoom** | A square closes in from the edge to the centre on every step: the reverse of Rings. | RATE SIZE |
| 66 | **Aim** | Crosshairs: a line moving up and down and another moving side to side, at different speeds. Where they cross is brightest. | RATE |
| 67 | **Propeller** | Thin blades click round a sixteenth of a turn per step. SIZE adds blades. | RATE SIZE |
| 68 | **Eight** | A dot with a short tail traces a figure of eight, once every four steps. | RATE |
| 69 | **Helix** | A DNA double helix: two waves winding round each other, with rungs between them. | RATE |
| 70 | **Metronome** | A metronome arm swinging from side to side, reaching the end on every step. | RATE |
| 71 | **Cradle** | Newton's cradle: the end ball swings out and clacks back on every step, left then right. | RATE |
| 72 | **Piston** | Four pistons pumping up and down one after another, like an engine. | RATE |
| 73 | **Ping** | A block ricochets from corner to corner, reaching a corner on every step. | RATE SIZE |
| 74 | **Juggle** | Three balls juggled hand to hand, one landing on every step. | RATE |
| 75 | **Frame** | Square frames step in from the edge one per step, then back out again. | RATE |
| 76 | **Stairs** | A staircase builds one stair per step, flashes, and starts again. | RATE |
| 77 | **Swap** | Two squares trade corners on every step, sliding past each other. | RATE |
| 78 | **Bubbles** | A bubble rises on every step and pops at the top. SIZE makes the bubbles bigger. | RATE SIZE |
| 79 | **Train** | A train runs round a square track, two pads per step. | RATE |
| 80 | **Boxes** | Two square outlines slide apart and back together on every step, overlapping in the middle. | RATE |
| 81 | **Bass** | A floor that rises and falls with the bass, its top edge brightest. | - |
| 82 | **Pads** | A 4x4 drum-pad grid: the kick, snare, bass and hi-hats each light their own pads as they play. | - |
| 83 | **Tally** | Every drum hit adds a dot in its colour, filling the grid over four bars, then it starts again. | - |
| 84 | **Beats** | Four columns for the four beats of the bar. The current beat's column is full and drains away as the beat passes. | - |
| 85 | **Lift** | A lift climbs a floor per step to the top, then comes back down. | RATE |
| 86 | **Hoop** | Basketball: a shot on every kick arcs into the hoop, and the net flashes as it drops through. | - |
| 87 | **Bowl** | Bowling: the ball rolls up the lane on every step and scatters the pins. | RATE |
| 88 | **Darts** | Three darts fly into the board, one per step, then the board clears. | RATE |
| 89 | **Seesaw** | A seesaw tips the other way on every step and the ball rolls down to the low end. | RATE |
| 90 | **Yoyo** | A yo-yo drops down its string and climbs back up on every step. | RATE |
| 91 | **Coil** | A new colour coils in from the edge to the centre along a spiral, once per step. | RATE |
| 92 | **Volley** | A ball lobbed back and forth over a net, landing on each side on every step. | RATE |
| 93 | **Swing** | A playground swing reaching the top of its arc on every step. | RATE |
| 94 | **Goal** | Penalties: a shot at goal on every kick while the keeper shuffles across, a pad per step. A goal lights the net. | RATE |
| 95 | **Skate** | A skater rides a halfpipe, reaching the lip on one side, then the other, on every step. | RATE |
| 96 | **Mow** | A new colour is mown in row by row, back and forth like a lawnmower, once per step. | RATE |
| 97 | **Pour** | A new colour pours in and fills the grid from the bottom up once per step, its surface sloshing. | RATE |
| 98 | **Ski** | A skier slaloms down through gates that scroll past, two rows per step. | RATE |
| 99 | **Pool** | Pool: on every step the white ball strikes the coloured ball into a corner pocket. | RATE |
| 100 | **Weave** | A new colour threads in along a maze-like path that visits every pad, once per step. | RATE |

### The six soft scenes

After the 100 come six scenes from the organic first build: **Aurora**, **Nebula**,
**Tide**, **Fireflies**, **Reeds** and **Lava**. They render a 32x32 field averaged down to
the pads, as the first design described. They don't step, so the Look knobs map onto their own
controls:

| Knob  | Soft-scene meaning |
|-------|--------------------|
| COLOR | the nearest gradient: Ice→Ink, Fire→Ember, Acid→Neon, Sunset→Dusk, Mint→Moss, Royal and Candy→Orchid, Mono→Bone, Rainbow→Prism |
| RATE  | flow speed (1 bar slowest, 1/16 fastest) |
| SIZE  | detail |
| TRAIL | bloom: glow and persistence |
| FOLD  | the same mirror fold, on the pads |
| SHIFT | unused |
| REACT | how hard the music pushes: Off 0, then 0.45, 0.6, 0.8 and 1 |

Their warp and drift sit at fixed middle values.

---

## Later: Journey and drift

The organic plan had Viz wander by itself: genes on slow random walks, palette drift, and a
**Journey** mode that dissolved from scene to scene on phrase boundaries. None of that is in
the geometric set. The user asked for control that feels played, and a show that changes by
itself works against that. Revisit after the device pass, if a hands-off mode is wanted; CYCLE
on the Settings page is kept for it.

---

## The Sequence page

The user, 2026-10-11: a third page "related to sequencing of scenes, of colours, of speeds",
up to eight slots, each choosing what plays and for how long; pick a step, then edit it.

- **Eight steps take turns at the Look page.** A step sets the scene and, if it says so, the
  colour set, rate, size and fold. Anything a step leaves at **Same** stays whatever the Look
  page says, so TRAIL, SHIFT, REACT and every Same field are still played live.
- **PLAY** starts it. It is **Off by default**, so Viz opens on the Look page's settings and
  the sequence is something you turn on.
- **It follows the song.** Steps run from beat 0 of Move's clock (or of Viz's own beat when
  stopped), so the same bar always shows the same step, and restarting Move starts at step 1.
  A jump ahead walks the sequence forward to where it would be.
- **BARS** is how long a step plays: 1 beat, 2 beats, 1, 2, 4 or 8 bars. **Off** skips the
  step, so the loop is any length from one step to eight. Step 1 starts at 1 bar, the others
  Off.
- **Random** is the first option of every step field. It draws again each time the step
  comes round, from all of that field's values (any of the 106 scenes, any length from 1 beat
  to 8 bars). The draw is seeded by where in the song the step starts, so a song played
  twice lights the same way twice. **Defaults:** scene and colour Random; rate, size and fold
  Same.
- **Step RATE reaches further than the Look page's**: 4 bars and 2 bars, then 1 bar to
  1/16. The scene engine takes a steps-per-beat override for the two long ones
  (`viz_shape_knobs_t.steps_per_beat`).
- **Edits apply at once.** Turning a playing step's BARS changes its length now; turning it
  Off moves on.

**Choosing a step: gates, not a rack.** STEP (`step`, "1"–"8") is a gate key. All 48 step
knobs (`s1_scene` … `s8_fold`) are declared on the one level, each with
`visible_if: {"param": "step", "equals": "N"}`, ordered so the gates leave STEP, SCENE,
BARS, PLAY over COLOR, RATE, SIZE, FOLD. A gate re-plans the moment its own knob writes it,
so turning STEP swaps the knobs at once (Strut's page-within-page, eight ways). STEP is where
the page is looking, not part of the show, so it is not saved with the set. Rejected: one
set of "edit" knobs that read and write the chosen step; the host caches values per key and
would show the previous step's until it polled again.

## The Mod page

Two LFOs, one a row, each with Strut's modulator knobs: **KIND, RATE, AIM, DEPTH**. Strut's
CURVE shapes a drum's fall and has no meaning for lights, so each LFO has one AIM and the
page holds two.

- **KIND:** Sine, Ramp and Square swing either way; Random holds a new value each cycle;
  Kick jumps to full on every kick and falls over RATE's length; Level follows how loud the
  music is.
- **RATE:** 1/16, 1/8, 1/4, 1/2, then 1, 2, 4, 8 and 16 bars, always on the beat.
- **AIM:** any Look knob (Scene, Color, Rate, Size, Trail, Fold, Shift, React), **Light**
  (brightness) or **Turn** (rotation). Not the Sequence page's own knobs: the LFOs move what
  the sequence chose.
- **DEPTH** (−1 to +1) is how far and which way. On a knob with steps it is how many steps
  the knob can move, all of them at full depth: Scene ±8 scenes, Color ±8 sets (both wrap
  round), Rate ±6 of the seven rates, Turn ±3 quarter turns (wraps), the rest their whole
  range. On Light it can only dim: full depth dips to dark at the LFO's low point.
- **They never turn a knob** (as Strut and Quilt): each frame the render thread copies the
  look, applies the sequence, then the LFOs, and draws that. The knobs show what was set.

Rejected: Schwung's own Master FX LFOs (Master FX Settings). They live outside Viz's pages,
and the user asked for Strut's setup on a page of Viz's own.

## Knob pictures

Every Look, Sequence and Mod knob draws a picture of what it does (`src/canvas.js`,
`viz: {kind: "custom:viz"}`, `canvas_script` in module.json), in the hand of Strut's and
Quilt's: one-pixel lines, and only time controls move (RATE, TRAIL and the LFOs' RATE, while
touched or for 1.5 s after turning). Settings keeps the host's own. They were drawn and
approved in a mockup first (`tools/gallery/pages.src.html`, built by `make_pages.mjs` into
`build/pages.html`; the first draft, before the user's Random, BARS and RATE changes): the Move's screen pixel for pixel in Schwung's label font, beside a
Matrix playing what the pages say.

- **SCENE** draws the scene itself, a dot a pad, with its number. The thumbnails are each
  scene's busiest frame (up to 40 lit pads) in its first 8 s, baked from the gallery
  (`tools/gallery/make_canvas_data.mjs`, 8 bytes a scene) with the scene names, each colour
  set's brightness and the device's 4x5 font. The tests fail if that block falls behind the
  gallery. A soft scene draws a glow.
- **COLOR** three swatches, each as bright as its colour, and the set's name. **RATE** a bar
  with a tick a step. **SIZE** a corner of the grid with the size filled. **TRAIL** a light
  and its trail. **FOLD** a mark folded the knob's way, mirror lines dotted. **SHIFT** three
  colours trading places, and when. **REACT** a kick and what it does to a shape.
- **STEP** the eight steps: the one being edited solid, a skipped one a dot, the one playing
  underlined. **BARS** a length bar. **PLAY** a play sign and the loop's length. Random is a
  die; Same says SAME.
- **KIND** the LFO's shape, **RATE** that shape at its speed with the speed in figures,
  **AIM** an arrow into a small picture of the target, **DEPTH** how far and which way.
- **STEP and PLAY read the whole sequence** through one read-only key, `seq_now`
  ("playing|steps|beats|bar", for example `2|1R100000|0|4`), declared as the pictures'
  `viz.extra_keys` so the host polls it on the page's rotation.
- A value that arrives as an index draws as the option it stands for.

---

## The light engine: how 8x8 becomes beautiful

### Threads

The scenes are too heavy for the audio callback, which is the SPI callback and the whole
device's deadline (`plugin_api_v1.h:29-104`). So:

- **Audio thread** (`process_block`):
  - runs the analysis (biquads, envelopes, flux: a few hundred multiply-adds per sample
    block, no FFT);
  - publishes a **feature frame** (bands, hits, beat, phases, energy) through a seqlock;
  - **sends MIDI**.
- **Render thread:**
  - created in `create_instance`. For a Master FX that runs on the shim's loader worker, so
    the thread inherits SCHED_OTHER instead of FIFO 70 (CLAUDE.md "Loading an FX module is
    OFF the callback");
  - wakes 30 times a second, reads the latest feature frame, renders, applies LIGHT and
    the power guard, and publishes the finished frame (8-bit RGB for every light) through a
    **triple buffer**, so neither side ever waits;
  - the audio thread takes the newest frame, diffs it against what each light shows, and
    builds and sends the messages. Keeping the diff on the audio side is what lets a refused
    packet stay pending without the two threads arguing about what was shown;
  - is joined in `destroy_instance`.
- **Only the audio thread calls `midi_send_external`.** The external ring's two producers
  both live on the SPI callback, and "a producer added on a worker thread would break it"
  (`src/host/ext_midi_ring.h:9-11`).

### Rendering

**The 100 geometric scenes** (`shapes.c`) draw at pad resolution. Each frame:

1. **Draw.** The scene calls `set(x, y, colour, amount)` for the pads it lights. Positions
   round to the nearest pad (JavaScript's `Math.round`), and two shapes on one pad keep the
   brighter. `colour` is 0, 1 or 2: one of COLOR's three colours.
2. **Colour.** The colour is the set's colour `(ci + shift) % 3`, where SHIFT advances
   `shift` on each bar, beat or kick. *Rainbow* instead takes a hue from the colour, the
   shift and the pad's position.
3. **FOLD** copies the top-left of the picture onto the rest: across (Sides), down (Top),
   both (Four), or both plus the diagonal (Eight).
4. **REACT** works on every scene:
   - Glow: brightness × (0.55 + 0.45·kick) × 1.25;
   - Grow: SIZE + 1 while the kick is above half, and brighter;
   - Flash: a wash of the third colour, 0.45 × kick, over the whole grid;
   - Flip: on a strong kick, lit pads go dark and dark pads light in the second colour.
5. **TRAIL.** Each pad shows the brighter of this frame and the last one faded by
   exp(−dt/τ), with τ = 0, 0.06, 0.15, 0.35 or 0.9 s.

**The six soft scenes** (`scenes.c`) render a **32x32 float field**, each pad the box
average of its 4x4 samples, with bloom adding blur and persistence, then the FOLD.

Then, for both kinds: TURN, the underglow (Glow: each side carries 0.8 × the pad beside it
for a geometric scene, a blend of the outer two rows for a soft one; Breathe: the beat pulses
the whole ring), LIGHT, and the power guard. Changing between a geometric and a soft scene
starts the new one fresh.

### Colour: full RGB by SysEx

The probe showed that SysEx reaches the Matrix, so **every light gets its own colour**:
6 bits per channel (262,144 colours), with no palette in the way. That is what makes
dissolves, drifting palettes and soft glows possible, since none of them fit a fixed set of
128 colours.

- **Pipeline:** linear RGB → POWER guard → LIGHT → gamma → 6-bit RGB.
- **One message per frame:** all of a frame's changed lights go in one `5E` message
  (`F0 00 02 03 4D 58 5E (idx r g b)… F7`, Apollo index for pads and underglow). That costs
  8 + 4n bytes: ≈ 1.33 packets per light, plus 3.
- **Whole-grid changes:** a frame where every light is the same colour (a beat flash, a
  fade to black) is sent as `5E` index 0, which fills everything in 4 packets. In the probe's
  Beat test, 96 separate notes at 2,000/s reached the late pads after the flash had already
  faded. A frame that is "mostly one colour" is sent as fill plus exceptions.
- **The power guard.** Before sending, Viz estimates the frame's draw as Σ(R+G+B) over the
  lights, in 8-bit units. In **POWER Port** mode, a frame over the budget is scaled down as a
  whole, so the picture dims but keeps its shape. **Underglow counts double** in the estimate,
  and the **budget is 33,000**. Both come from the probe at the Matrix's maximum brightness
  (RESULTS.md, "Power"):
  - pads alone froze at `#DADADA` and were safe at `#B6B6B6`;
  - underglow alone froze at `#FFFFFF` and was safe at `#DADADA`;
  - all 96 lights froze at `#919191` and were safe at `#6D6D6D`.

  Together these put one underglow light at 1.4–2.0 pads, so ×2 is the cautious end. They
  put the freeze point above ≈ 36,500 pad-units in every case that fits the data, and 33,000
  leaves a 10% margin. So the guard allows pads alone up to `#ABABAB` and all 96 lights up to
  `#565656`. It is safe at any Matrix brightness setting. **POWER Hub** turns the guard
  off, for a powered hub.
- **Fallback: notes.** If SysEx stops working (another Matrix OS version, another device),
  the same frame is sent as Note On: the nearest colour across the two built-in palettes (ch
  1 + ch 2, ≈ 250 colours) from a precomputed OKLab LUT. It costs 1 packet per light. It is
  chosen automatically if the Matrix never shows a SysEx frame. Not detectable today, so it
  is a hidden setting for now.
- **Rejected for now:** custom palettes (`41 3D`) per scene. They work, but rewriting a
  palette doesn't recolour pads already lit (the colour is looked up when the note arrives,
  `NoteHandler` :158), so drifting colours would still mean re-sending every pad. Kept as a
  bandwidth trick for Graphic scenes, if direct RGB proves too heavy.

### Sending: only what changed, within budget

`host->midi_send_external` queues 4-byte USB-MIDI packets onto a 64-packet ring. The ring is
drained into an 80-byte (20-packet) MIDI_OUT region once per block, **shared with Move's own
output, the LED queue and the shadow UI** (`plugin_api_v1.h:182-200`; `ext_midi_ring.h:33`;
`schwung_shim.c:1743-1760`; `shadow_constants.h:83`). It returns 0 when full and promises
nothing more.

- **Packets:** cable 2 SysEx packets (CIN 4–7: `0x24`–`0x27`), or for the fallback, Note On
  `0x29, 0x90 | ch, note, index`. Set cable 2 explicitly, as the chain's knob CC out does
  (`src/modules/chain/dsp/chain_params.c:1067-1073`). The probe used this and the host never
  refused a packet.
- **Diff:** Viz keeps what it believes each of the 96 lights shows. A light is sent only when
  its wanted colour differs by more than a small perceptual threshold (OKLab ΔE), so tiny
  flickers in a slow drift don't spend bandwidth.
- **Order:** the biggest perceptual change goes first, so under pressure the picture
  degrades into a slight lag, not a smear of wrong pads. A frame's message is capped at the
  budget, and the rest wait for the next frame.
- **Budget: ≈ 2,000 packets per second** (≈ 6 per block, evenly spaced). The probe saw no
  freeze or loss up to ~6,900/s, and 2,000/s looked right, so this leaves headroom and
  leaves 14 of the 20 MIDI_OUT slots per block for everyone else. At 30 fps that is ~66
  packets per frame, or about 48 changed lights.
- **Retry:** on a 0 return the pad stays dirty, and **a refused packet is never recorded as
  sent**. A pad believed lit but never sent would stay wrong forever; the same rule as
  `knob_emit_cc_out` (`chain_params.c:1075-1086`).
- **Refresh: every light is re-sent at least every 2 s**, a quarter of them every 0.5 s in
  rotation. This heals any light the Matrix missed, for example across a replug, and any
  Move note Viz didn't hear (*Move's own notes*). There is no hotplug signal, and packets sent with nothing attached simply go
  nowhere.
- **Lights off: only through LIGHT.** Bypassing a Master FX doesn't stop it: Schwung still
  calls `process_block` and only restores the dry audio afterwards
  (`schwung_shim.c:3752-3768`), and Viz never touches the audio anyway. Removing Viz runs
  `destroy_instance` on the loader thread, where sending MIDI is not allowed
  (`ext_midi_ring.h:9-11`), so **the grid keeps its last picture after Viz is removed.**
  help.json says: turn LIGHT to 0 first, or replug the grid. LIGHT at 0 sends black.
- **A shared ring, shared with overtake modules.** Master FX sends are tagged
  `EXT_SRC_OVERTAKE`, so an overtake module unloading discards Viz's queued packets
  (`schwung_shim.c:2105`) after they were counted as sent. A SysEx message cut in half
  times out on the Matrix after 1 s (`OS/MIDI/MIDI.cpp:11`), and the refresh repaints
  whatever was lost within 2 s.

### Move's own notes

Move sends each track's notes out of USB-A, and the Matrix's Performance app paints a pad
for any Note On, Note Off or CC on channels 1–6 (`Performance8x8.cpp:158-192`), whatever
the port. Left alone, drums blink pads under the show.

**Viz takes the place of each one.** The shim scans Move's cable-2 MIDI_OUT before each
transfer and hands every note to the Master FX as EXTERNAL (`schwung_shim.c:1478-1640`,
`shadow_midi.c:430-435`), through the exported `move_audio_fx_on_midi`, which the host looks
up by name (`shadow_chain_mgmt.c:1766`; the v2 struct's `on_midi` is never called for a
Master FX). The packet is still waiting in the MIDI_OUT region of the mailbox every plugin
is handed (`mapped_memory`, offset 0), so Viz overwrites it there with its **cover**: one
Note On for the same pad. The Matrix never receives Move's note. Schwung's own slots already
had it (the scan dispatches before the Master FX), so Schwung instruments still play.

- **Measured** (2026-10-11): first, the cover was *queued* instead. Viz heard and covered
  every note, yet every note still showed. A queued packet drains into the first empty
  slot of the same transfer, which can sit ahead of Move's note, so Move's note landed
  last. Replaced in place: 4,228 of 4,228 notes, none seen on the grid.
- **The cover's colour is exact on lit pads.** A pad Move's notes hit becomes *hot*. Viz
  keeps two entries for each hot pad, in the Matrix's custom palettes 1 and 2 (channels 3
  and 4, written with `41 3D`, to RAM only; Viz never sends the `41 7D` that saves them):
  - one entry holds what the pad shows now;
  - the other holds what it will show next, written before the frame that changes it.

  A hot pad takes a new colour only once a palette entry holds it, a few ms late, so the
  cover (`9x note id`) restores exactly what was there. Only a pad's first hit uses the
  nearest built-in colour, never brighter. With the palettes up to date, 95% of hits on
  lit pads in a Rainbow scene keep their exact colour (test); the rest are first hits.
  Palette writes take turns with frames and only cover hot pads, so they cost little.
  A custom palette a user made in Performance is overwritten in RAM while Viz runs; the
  saved copy is untouched.
- **The exact colour follows** anyway: the pad is marked unknown, which puts it first in the
  next frame message.
- **When Move's packet isn't in the mailbox** (a note from another path), the cover is
  queued instead, at most 4 a block, and Viz ignores its own queued covers coming back.
- **What it can't see:** a note on a channel the Master FX **MIDI Ch** setting filters out.
  Left on All (the default) Viz hears them all; otherwise the 2 s refresh heals the pad.
- Move's own pads arrive as INTERNAL and never reach the grid, so they are ignored.
- On a Launchpad (Programmer mode, notes on channels 1–3 paint the pad of that index) Viz
  marks the light for re-sending but sends no cover; untested.
- **Diagnostics:** every 2 s while MIDI comes in, Viz logs a `viz: midi` line (notes heard
  by source, pads hit, covers, replaced, refused, and the last four messages).

### TURN

TURN rotates the picture in 90° steps at the pad-mapping stage, so the show matches however
the Matrix sits beside the Move. Scenes and the underglow sides rotate together.

---

## Control surface

Four pages: page 1 holds the knobs on `root`, and links to the `sequence`, `mod` and
`settings` levels add pages 2 to 4, in that order. The user put Settings last and named the
new pages SEQ and MOD (2026-10-11); the header shows the link's label. A flat `chain_params` list declares every key, because without
it the Master FX cache reads only the first `params` array
(`src/host/shadow_chain_mgmt.c:1711-1740`). Wayward is the working example
(`wayward/src/dsp/wayward.c:1281-1335`). Schwung appends its own *My Presets* and *Module*
pages after these.

Every `short_name` is a real word of at most 5 characters, drawn verbatim (checked with
`labelVerbatim()`, `render_page_movy.mjs:311-346`). The full meaning is in `name`, which the
held-knob header shows.

### Page 1: Look

These shape the picture, and each one shows its effect straight away.

| Knob | Key | Label | Behaviour |
|------|-----|-------|-----------|
| 1 | `scene` | SCENE | **Scene.** Enum: the 100 geometric scenes in gallery order, then the six soft scenes. Default Pulse. |
| 2 | `color` | COLOR | **Colour set.** Enum: Ice, Fire, Acid, Sunset, Mint, Royal, Candy, Mono, Rainbow. Three colours each; Rainbow spreads hues across the grid. Default Acid. |
| 3 | `rate` | RATE | **Steps, on the beat.** Enum: 1 bar, 1/2, 1/4, 1/8, 1/16: one step per bar up to one per 16th. Default 1/4. |
| 4 | `size` | SIZE | **Shape size (pads).** Enum: 1, 2, 3, 4. How big or thick the shapes are. Default 2. |
| 5 | `trail` | TRAIL | **Trail length.** Enum: Off, Short, Medium, Long, Smear. How long lights linger after they move on; Off is hard-edged. Default Short. |
| 6 | `fold` | FOLD | **Mirror fold.** Enum: Off, Sides, Top, Four, Eight. Folds the picture left/right, top/bottom, four ways or eight ways (a kaleidoscope). Default Off. |
| 7 | `shift` | SHIFT | **Colour swap.** Enum: Hold, Bar, Beat, Kick. When the three colours trade places. Default Bar. |
| 8 | `react` | REACT | **Kick reaction.** Enum: Off, Glow, Grow, Flash, Flip. What a kick does to any scene (see *Rendering*). Default Glow. |

### Page 4: Settings

| Knob | Key | Label | Behaviour |
|------|-----|-------|-----------|
| 1 | `mode` | MODE | **Sync mode.** Enum: Auto / Beat / Sound / Blend (see the table under *Mode*). Default Auto. |
| 2 | `power` | POWER | **Power source.** Enum: Port / Hub. *Port* (default) keeps every frame inside what the Move's USB-A port can supply (measured; see *The power guard*). *Hub* removes the limit, for a Matrix on a powered USB hub. |
| 3 | `light` | LIGHT | **Brightness.** Overall level, 0–100%, applied after the power guard, so it can only make things dimmer. Default 50%. |
| 4 | `grid` | GRID | **Grid device.** Enum: Matrix / LP Mini MK3 / LP X / LP Pro MK3 / LP MK2 / LP Pro (see *Other grids*). Default Matrix. Launchpads are untested. |
| 5 | `cycle` | CYCLE | **Journey phrase.** Enum: 4 / 8 / 16 / 32 / 64 bars. Kept for a later Journey (see *Later: Journey and drift*); it does nothing yet. Default 16. |
| 6 | `turn` | TURN | **Rotation.** Enum: 0° / 90° / 180° / 270°. Default 0°. |
| 7 | `edge` | EDGE | **Underglow.** Enum: Off / Glow (each side carries the picture's colour beside it) / Breathe (the beat pulses the whole ring). Default Glow. |
| 8 | `nudge` | NUDGE | **Light timing.** −150 to +100 ms. It shifts the lights earlier or later to match the sound you hear. **Default −40 ms**: driven straight off the clock, the lights ran about 40 ms ahead of the speakers (probe Beat test). |

---

### Page 2: SEQ

STEP, SCENE, BARS and PLAY over COLOR, RATE, SIZE and FOLD. The six step knobs are the chosen
step's (keys `s1_scene` … `s8_fold`).

| Knob | Key | Label | Behaviour |
|------|-----|-------|-----------|
| 1 | `step` | STEP | **Step to edit.** 1–8. Not saved. |
| 2 | `sN_scene` | SCENE | **Step N scene.** Random, then the 106 scenes. Default Random. |
| 3 | `sN_bars` | BARS | **Step N length.** Off, Random, 1 beat, 2 beats, 1 bar, 2, 4, 8 bars. Default 1 bar for step 1, Off for the rest. |
| 4 | `play` | PLAY | **Play the sequence.** Off / On. Default Off. |
| 5 | `sN_color` | COLOR | **Step N colour set.** Random, Same, then the nine sets. Default Random. |
| 6 | `sN_rate` | RATE | **Step N steps, on the beat.** Random, Same, 4 bars, 2 bars, 1 bar, 1/2, 1/4, 1/8, 1/16. Default Random. |
| 7 | `sN_size` | SIZE | **Step N shape size.** Random, Same, 1–4. Default Random. |
| 8 | `sN_fold` | FOLD | **Step N mirror fold.** Random, Same, Off, Sides, Top, Four, Eight. Default Four. |

### Page 3: MOD

| Knob | Key | Label | Behaviour |
|------|-----|-------|-----------|
| 1, 5 | `m1_kind`, `m2_kind` | KIND | **LFO shape.** Sine, Ramp, Square, Random, Kick, Level. Defaults Sine, Kick. |
| 2, 6 | `m1_rate`, `m2_rate` | RATE | **LFO speed.** 1/16 … 16 bars. Defaults 1 bar, 1/4. |
| 3, 7 | `m1_aim`, `m2_aim` | AIM | **LFO target.** Scene, Color, Rate, Size, Trail, Fold, Shift, React, Light, Turn. Defaults Color, Size. |
| 4, 8 | `m1_depth`, `m2_depth` | DEPTH | **LFO depth.** −1 to +1. Default 0: the LFOs start silent. |

---

## Rejected alternatives

- **Organic scenes as the main set.** The first build's scenes (auroras, ink, fireflies…)
  looked random on the grid, and their knobs had no effect you could point to. Six remain as
  soft scenes after the 100.
- **The organic knobs: FLOW, GROW, BLOOM, WARP and DRIFT.** Each moved a picture that was
  already moving, so turning them was hard to see. RATE, SIZE, TRAIL, FOLD and SHIFT each
  make one visible change.
- **MIRROR as the fold knob's label.** Six letters; labels stop at five. FOLD says the
  same.
- **Scenes the user dropped in the gallery** (2026-10-10): Chevron, Checker, Life, Blocks,
  Sparkle, Split, Zigzag, Fill, Laser, Star, Bricks, Tiles, Marquee, Orbit, Frog, Rocks,
  Bolt, Signal, Fish, Triangle, Grid, Domino, Meter, Level, Ghost, Loader, Cube, Peaks,
  Simon, Cat, Notes, Blinds, Open, Golf, Pogo and Fountain.
- **Moving Move's tracks to channel 7 or higher** (the first fix for Move's notes: the
  Matrix ignores those channels). It works, but it costs a setting on every track and on
  every Schwung slot's Recv Ch; covering the notes inside Viz (*Move's own notes*) costs
  nothing.
- **Leaving Move's notes alone and covering after them.** The first build queued its cover;
  it reached the Matrix ahead of Move's note and lost (measured). Replacing the packet in
  the mailbox is the only order Viz controls. It touches only notes that would paint a
  Matrix pad (channels 1–6), and only while GRID is Matrix.
- **Move's MIDI In Only setting.** It stops Move's notes reaching USB-A, but Schwung hears
  Move's tracks through that same output, so it would silence Schwung instruments.
- **Only marking the light for re-sending.** The SysEx stream is mid-message most of the
  time, so the exact repaint lands 10–60 ms late: a visible tick on every drum hit.
- **Built-in palette colours for every cover.** Close, but on a lit pad the change of shade
  showed (the user, 2026-10-11). The custom palette entries make it exact.
- **Organic scenes leading, with icons as a side family** (the first design). The user's
  device verdict reversed it: geometric scenes lead.
- **A JavaScript canvas or custom on-screen UI.** Fleet rule: knobs and the jog wheel only.
  The Matrix is the display.
- **Built-in palettes as the main path.** The first draft assumed SysEx couldn't reach a
  multi-port device (`E16_REMOTE.md:113-127`). The probe showed it does, so full RGB leads and
  the palettes are the fallback.
- **Temporal dithering (the first draft's SHADE knob).** It was only ever there to fake
  colours missing from the palettes. Full RGB makes it unnecessary, and its knob became
  POWER.
- **Per-scene custom palettes as the main path.** They work, but a palette rewrite doesn't
  recolour lit pads, so drifting colour costs a full repaint anyway.
- **A separate "EQ mode" and "sync mode" per scene.** The shared inputs make every scene work
  in every mode, so MODE only chooses the sources.
- **Sending all 64 pads every frame.** That would fill the entire shared external ring on
  every frame and starve Move's own MIDI out. Diff plus budget instead.
- **Rendering on the audio thread.** Fluids, reaction-diffusion and supersampling cost far
  more than the SPI callback can spare.
- **A gain knob.** Replaced by the automatic level.
- **Hard cuts in a Journey.** A later Journey would dissolve. Turning SCENE by hand cuts on the next frame, which suits shapes that step.
- **Tempo detection from audio** in 0.x. When stopped, the own beat from kick hits is
  enough. Revisit only if it proves unconvincing.
- **Saving the custom palette to flash** (`41 7D`). It wears the flash and overwrites the
  user's own palettes.
- **A Tool instead of a Master FX.** The user's choice (see *Where Viz sits*).
- **A RATE knob (20–60 fps).** It gave its place on Settings to GRID, because a ninth
  Settings knob splits the page in two. Viz renders at a fixed **30 fps**; the sender already
  degrades gracefully under load. The preview tool can still render at other rates.
- **Detecting the grid automatically.** Nothing comes back from USB-A to a Master FX.

---

## Implementation notes

- **Realtime:**
  - nothing in `process_block`, `set_param`, `get_param` or `on_midi` allocates, logs,
    locks, or touches files (`plugin_api_v1.h:29-104`);
  - all buffers (fields, simulation grids, LUT, rings) are allocated in `create_instance`;
  - the render thread is joined in `destroy_instance`.
- **Scene state:** a geometric scene keeps its state in a fixed 4 KB block inside
  `viz_shapes_t`, with fixed-capacity lists (64 entries) where the gallery grows arrays.
  The soft scenes keep one union of fixed-size structs per slot. A new scene is initialised
  by the render thread, never by `set_param`.
- **Parity with the gallery.** `shapes.c` reproduces the JavaScript's arithmetic exactly:
  doubles throughout, `float` where the gallery stores into a `Float32Array`, JavaScript's
  rounding (`floor(x + 0.5)`) and `ToUint32`, and its xorshift random numbers and hash.
  Everything builds with **`-ffp-contract=off`**: a fused multiply-add rounds once where
  JavaScript rounds twice, and that alone broke Pool and Bowl.
- **Determinism:** one seeded PRNG per instance (xorshift), so tests and the preview tool
  can replay a run exactly.
- **Pad map:** a `const uint8_t` table from (x, y) to note, built from the formula in *Which
  note is which pad*, with TURN applied when the pad index is computed.
- **State:** saved in the `state` JSON with all 16 keys. Genes are not saved; they re-seed.
- **module.json:**
  - `component_type: "audio_fx"`, `api_version: 2`, `"dsp": "viz.so"`;
  - `capabilities.chainable: true`, `audio_in`/`audio_out`;
  - `requires_continuous_processing: true`, so frames keep coming in silence;
  - `ui_hierarchy` and `chain_params` as above.
- **Skeleton to start from:** Loom's Master FX probe
  (`loom/probes/master-fx/src/dsp/penpal_probe.c`) already reads `get_beat_position` and
  `move_info`.
- **Packetising reference:** `src/modules/midi_fx/sysex_probe/dsp/sysex_probe.c:120-170`
  (cable 2, counting refusals).
- **Viz is the first Master FX to send to USB-A.** The catalog's `m8` and `control` drive
  external grids, but only as overtake modules from JS.
- **help.json:**
  - what Viz is;
  - setting up the Matrix (Performance app, Menu Lock; replug if it stays dark);
  - POWER: keep *Port* unless the Matrix is on a powered hub;
  - touching the Matrix plays the Move's selected track;
  - drums lighting pads: set the track's MIDI out to channel 7 or higher, and a Schwung
    slot's Recv Ch to match;
  - turn on Move→Schwung so Viz hears Move's own instruments, not only Schwung modules;
  - LIGHT 0 before removing Viz, or the last picture stays;
  - Launchpads: pick GRID; untested;
  - what each knob does, and that SCENE holds 100 shapes and then 6 soft scenes.

---

## Preview tool

**Built first, used for every scene.** `tools/preview` compiles the same scene, analysis and
quantisation code for the Mac. It takes a WAV, a tempo and a play/stop script, and writes
one self-contained HTML page showing:

- the 8x8 grid exactly as the Matrix would show it (real palette colours, two channels,
  underglow);
- the 32x32 field it came from;
- the feature traces (bands, hits, beat, energy).

The page plays, scrubs, and switches scenes.

This is how Viz gets beautiful. Each scene is tuned by eye on real music, and the user judges
each family from preview pages before it reaches the device. The device pass then corrects
only what the LEDs themselves change: brightness response, colour cast, viewing angle.

---

## Build order

0. ~~**Probe: `viz-probe`.**~~ Done 2026-10-10; see [probe/RESULTS.md](probe/RESULTS.md).
   - The Matrix connects (sometimes only after a replug).
   - The pad and underglow maps are right.
   - SysEx `5E` and `41 3D` both work.
   - Throughput isn't a limit (no loss up to ~6,900/s).
   - **Power is** the limit, and has been measured.
   - Master FX hears Schwung's slot modules always.
   - NUDGE ≈ −40 ms.
   - The render bench is ~0.5 ms per frame.
   - The `5E` fill-all flash lights every pad and the underglow together, in 4 packets.
   - Move's own instruments reach Master FX only with Move→Schwung on.
   - Master FX never receives anything from the USB-A device (pad presses count 0).
1. **0.1.0: the engine and the first scenes.**
   - Preview tool.
   - Light engine: render thread, seqlock, triple buffer, full-RGB `5E` frames with fill-all,
     perceptual diff, power guard, budget, retry, rotating refresh, all-off.
   - Features: clock, 8 bands, three hits, automatic level, own beat.
   - All four MODEs and both pages.
   - The device layer (GRID): the Matrix, plus five Launchpads, untested.
   - ~~Organic scenes: Aurora, Nebula, Tide, Fireflies, Reeds, Lava.~~ Built, then judged
     on the device and moved behind the main set.
   - **The reversal (2026-10-10):** the scene gallery, six pick rounds, and the 100
     geometric scenes ported to C with exact parity. The new Look page: COLOR, RATE, SIZE,
     TRAIL, FOLD, SHIFT, REACT.
   - ~~Next: the device pass with the 100 scenes.~~ The user: "The scenes look right on the
     Matrix" (2026-10-11).
   - **The Sequence and Mod pages, and knob pictures** (2026-10-11), mocked up and approved
     first. Next: their device pass.
Versions 0.2.0–0.4.0 below are the organic plan. After the device pass they will be
redrawn around the geometric set.

2. **0.2.0: Journey.**
   - Organic: Ink, Coral, Pollen, Moiré, Caustics, Bloom.
   - Graphic: Rings, Mirror, Steps.
   - The noise-mask dissolve, Journey (all three kinds), CYCLE.
3. **0.3.0: Evolution.**
   - Organic: Moss, Lichen, Flock, Comets, Julia, Cells.
   - Graphic: Radar, Spiral, Checker.
   - Gene drift (DRIFT), palette drift, energy arc, section changes, EDGE, dreaming.
4. **0.4.0: The full set.**
   - Organic: Smoke, Stars, Dawn, Lissajous, Sea, Rain.
   - Graphic: Tunnel, Life, Sparkle.
   - The note fallback (two built-in palettes, OKLab LUT).
5. **0.5.0: Finished.** Every scene tuned on the device, `help.json`, a manual entry,
   release, and the catalog entry.

---

## Testing

`tests/run.sh` builds `viz.so` with `-Wall -Wextra -Werror` and drives it black-box through
the v2 API with a fake host. The host records every `midi_send_external` packet, can refuse
packets on a schedule, and serves a scripted `get_beat_position` and `get_bpm`.

**MIDI**
- No block ever sends more than 6 packets, or 2,000 per second averaged.
- Every SysEx frame parses: header `F0 00 02 03 4D 58 5E`, (idx r g b) groups with idx a
  valid Apollo index for one of the 96 lights, 6-bit values, `F7`.
- In POWER Port, no frame's estimated draw exceeds the budget; in Hub, frames are untouched.
- Every light is re-sent within 2 s, even when nothing changes.
- Every refused packet is re-sent, and nothing refused is ever recorded as shown. The fake
  host refuses 50% of packets, and the final believed grid must equal the wanted grid.
- Every packet is cable 2. Fallback notes are ch 1–2, with a note in 36–99, 100–123 or
  28–35.
- Bypass and `destroy_instance` leave all 96 lights off.
- The fill-all message is used only when it changes 24 or more lights, doesn't blank more
  correctly-lit lights than it fixes, and makes no light brighter than it should be. (Without
  those rules a mostly-dark picture kept refilling the grid black and never drew its lit
  pads, and a bright flash pushed the underglow past the power budget for a frame.)
- TURN rotates a known test frame correctly.

**Move's notes** (`test_move_notes`; the fake Matrix paints channel messages from its
palettes, even between SysEx packets)
- The note map matches Performance's drum rack and edge; channel 7, pitch bend and Move's
  own pads (INTERNAL) paint nothing and draw no cover.
- A cover is never brighter than the light, and exact for any palette colour.
- With the grid dark, 128 Move notes and note-offs on the drum pads are dark again within
  their own transfer; with Pulse lit, every hit light is sent again by SysEx within 80 ms;
  no SysEx message breaks.
- At most 4 queued covers a block, and Viz's own cover echoed back draws none.
- The fake mailbox ships Move's note in a later slot than queued packets, as on the device:
  queued covers lose 8 of 8; covers that replace Move's packet win, and touch no other slot.
- Drum hits on lit pads of a Rainbow scene keep their exact colour at least 90% of the time
  (95% measured), through custom palettes 1 and 2; the fake Matrix checks every `41 3D`
  write.

**Listening**
- A sine sweep raises the bands in order.
- A kick loop fires low hits on the kicks and no high hits.
- With the clock stopped, a 120 BPM kick loop gives an own beat that phase-locks within 4
  beats.

**Sequence and Mod** (`test_sequence`, driving `seq.c` directly)
- PLAY Off and LFOs at depth 0 leave the look untouched.
- A one-bar step and a two-beat step alternate on the right beats; going back to beat 0
  starts at step 1; a jump ahead lands where walking would.
- Random draws the same at the same song position in two instances, draws widely (over 40
  scenes in 2,000 beats), and stays inside each field's options; with every step Off nothing
  plays.
- A Square on Color swings both ways; Light only ever dims; Kick follows the kicks; a Ramp
  on Turn reaches all four turns.
- Through the plugin: the user's defaults, `seq_now` at rest and playing, and the new keys
  round-tripping through `state` (STEP left out).

**Pictures** (`tests/widgets.test.mjs`, through the host's own widget registry, frame and
viz resolver)
- Every option of every pictured knob draws inside its cell, on every step's page; next
  options look different; an unanswered value draws nothing; an index draws as its option.
- canvas.js knows every option list Viz serves, in order, and its generated block matches the
  gallery.
- STEP shows the playing step and which steps are on; PLAY the loop length; the LFO RATE its
  own KIND; RATE, TRAIL and the LFO RATE move only while touched or just turned.
- The plan test plans every STEP value with the host's planner: four pages (Look, SEQ, MOD,
  Settings), the SEQ page exactly STEP, SCENE, BARS, PLAY, COLOR, RATE, SIZE, FOLD of that step.

**Scenes**
- **Parity** (`tests/parity_dump.mjs`, `tests/test_parity.c`). Node runs the gallery's own
  script and writes every pad of every frame: 100 scenes, 540 frames (18 s) each, under four
  knob settings. The C runs the same drum loop, knobs and clock, and every value must match
  to within one level. Today: 41,472,000 values, none off by even one. The test also checks
  that the scene names and order are the gallery's.
- **Each Look knob is tangible.** Sweep, 8 s, each knob moved alone from its default: more
  than a quarter of the frames must differ.
- Every scene runs for 10 simulated minutes (music, then silence), at default knobs and at
  every knob maxed. Soft-scene fields stay finite (no NaN or inf); no scene is all-black for
  more than 2 s in the first 5 minutes, while the music plays; every frame renders in under
  4 ms.
- Frames are bit-identical from a fixed seed.
- Later, with Journey: genes stay within their bounds over 1 simulated hour, and a dissolve
  always reaches the next scene.
- Render time per frame stays under budget for each scene (host-side timing as a
  regression guard; the device number comes from the probe).

**Controls**
- Every `short_name` passes `labelVerbatim()` at the device cell width, as Quilt's tests do.
- Every key in `chain_params` appears in `ui_hierarchy` and round-trips through
  `state`.

**By eye.** Preview pages for each family go to the user before each version. Then there is a
device pass with the Matrix on the Move: Move's own instruments and Schwung modules,
Move→Schwung off and on, playing and stopped, every scene at LIGHT 80% and 100%, and with the
Matrix at its maximum brightness in POWER Port (it must never freeze).
