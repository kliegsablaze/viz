# Viz

*Viz grows living light on a Matrix out of whatever the Move is playing. Its pictures are
abstract and organic: they breathe with the beat, listen to the mix, and keep changing over a
whole song.*

**Status:** design, with the device probe done (2026-10-10). No Viz code yet. Written against
Schwung **v1.7.3** and Matrix OS commit **e1a4c08** (2026-09-25); the user's Matrix runs Matrix
OS **3.2**.

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

- **Move playing:** the pictures lock to Move's clock. Petals open across a bar, fireflies
  fall into step, scenes change on phrase boundaries.
- **Move stopped:** the pictures follow the sound coming out of the master. Bass swells,
  hats glint, the mix's own hits make the beat.
- Either way the show **evolves on its own.** Every scene drifts slowly, colours wander, and
  in Journey mode Viz moves from one scene to the next, dissolving one world into another.

Viz leaves the audio untouched. It is an "effect" only in where it sits.

### The aesthetic brief

The user, 2026-10-10:

> *"I want beautiful, organic, abstract animations. Let's really use this to make a gorgeous
> visualizer with a very wide range of different animations that can evolve over time."*

and, on the icon-style pictures of the first draft:

> *"We can also keep the icon-style pictures. They're just other modes available."*

So the organic scenes lead: soft light moving through a medium, like ink, smoke, water,
auroras, growth and swarms. A crisp **Graphic** family (equalizer bars, rings, radar and the
rest) sits beside them as further modes.

An 8x8 grid only looks organic if three things hold, and the engine is built around them:

1. **Every pad is antialiased.** A pad is the average of a 4x4 patch of a finer picture, so
   an edge falls across a pad as a partial glow rather than a hard step.
2. **Motion is continuous.** Positions are real numbers and every musical input goes
   through a soft spring, so nothing jumps a whole pad at a time.
3. **Colour comes from designed gradients.** Each scene draws a *value* that is looked up
   in a cosine-palette gradient. Colours are not raw hues.

---

## Lineage

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
  Viz therefore **repaints every light regularly**, not only changed ones (see *Sending*),
  and help.json suggests turning off Move's MIDI output to USB-A.
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
4. Optional: turn off Move's MIDI output to USB-A, so Move's notes don't paint the pads.
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

One shared grammar keeps the show legible across 36 scenes:

| Musical event      | What it does to the picture                        |
|--------------------|----------------------------------------------------|
| Low hit (kick)     | an **impulse**: dye injected, a stone dropped, a bloom |
| Mid hit            | a **secondary ripple** or eddy                     |
| High hit (hats)    | **glints**, twinkles, sparks                       |
| 8 bands            | **pressure and density**, per column or per region |
| Beat phase         | **breathing** of brightness and scale              |
| Bar                | the **palette steps** forward                      |
| Phrase / section   | where **transitions** happen                       |
| Energy arc         | scene choice in Journey; palette warmth            |

**Nothing jerks.** Every input reaches a scene through a critically-damped spring. REACT sets
the spring's depth and FLOW its speed, so light swells and settles.

---

## Scenes

**36 scenes in seven families.** The six organic families (24 scenes) are the heart of Viz.
The seventh, **Graphic**, is the crisp icon-style set.

Each scene has 3–4 **genes**: internal parameters that drift (see Evolution). The tables give
each scene's look, its genes, and what the music does. The "beat" column assumes a clock or
the own beat; "sound" is the bands and hits.

### Sky

| Scene  | The picture | Genes | Beat | Sound |
|--------|-------------|-------|------|-------|
| **Aurora** | Curtains of layered noise hanging and folding across the grid | fold scale, curtain count, sway | curtains breathe | bands lift their own columns of curtain; hats shimmer the hems |
| **Nebula** | Domain-warped fractal clouds turning slowly | warp depth, octave count, rotation | warp pulses | kick punches the warp outward; brightness opens the colour |
| **Dawn** | A soft sun rising over a phrase and tinting the whole sky | horizon, glow width, haze | rise is phrase-locked | loudness lifts the glow; section change starts a new dawn |
| **Stars** | A drifting depth field of soft points | depth speed, density, parallax | the field surges | hats twinkle individual stars; kick sends a ripple through the field |

### Water

| Scene  | The picture | Genes | Beat | Sound |
|--------|-------------|-------|------|-------|
| **Tide** | A wave-equation pool (24x24) with soft reflections | damping, wave speed, depth tint | stones dropped on the beat | kick drops a big stone, mids small ones, at points chosen by the bands |
| **Caustics** | The light under moving water: a sum of drifting wave fronts | wave count, scale, flow angle | brightness breathes | bands set the energy of the matching wave fronts |
| **Sea** | The 8 bands as a rolling, foaming surface seen side-on: the **organic equalizer** | swell, foam, horizon | swell on the beat | each column's water level is its band; hits throw spray |
| **Rain** | Drops spreading rings on a dark pond | drop rate, ring speed, wind | drops on 8ths | hats and mids make drops; kick makes a heavy one |

### Fluid

| Scene  | The picture | Genes | Beat | Sound |
|--------|-------------|-------|------|-------|
| **Ink** | Stable fluids (16x16 solver, dye at 32x32): coloured ink curling through water | viscosity, vorticity, dye fade | a slow stir each bar | kick injects a jet of dye, its colour from the palette's current phase |
| **Smoke** | Curl-noise wisps rising and unravelling | curl scale, rise speed, turbulence | rise surges | bands feed smoke sources along the bottom row |
| **Lava** | Merging metaballs drifting like a lava lamp | ball count, threshold, buoyancy | the balls swell | bass swells them; mids split one off |

### Growth

| Scene  | The picture | Genes | Beat | Sound |
|--------|-------------|-------|------|-------|
| **Coral** | Gray-Scott reaction-diffusion (32x32): spots, stripes and coral fronts | feed rate, kill rate, diffusion ratio | growth pulse | hits seed new growth; the energy arc walks feed and kill through the parameter map |
| **Moss** | Physarum slime: agents leaving and following trails into veins | agent count, sensor angle, decay | trails flare | bands bias agents toward their regions; kick scatters them |
| **Lichen** | Lenia soft life: smooth creatures that glide and divide | kernel radius, growth centre, growth width | heartbeat | hits drop new life; quiet lets it settle into stable forms |
| **Bloom** | Phyllotaxis petals unfurling from the centre | petal count, angle offset, open speed | opens across each bar and closes on the downbeat | loudness opens it wider; hats flick the petal tips |
| **Reeds** | 8 soft grass stalks swaying: the **second organic equalizer** | stiffness, gust, seed heads | wind gusts on the beat | each stalk's height is its band |

### Swarm

| Scene  | The picture | Genes | Beat | Sound |
|--------|-------------|-------|------|-------|
| **Fireflies** | A few dozen soft lights, each a Kuramoto oscillator flashing on its own cycle | coupling, spread of natural rates, wander | **coupling to Move's clock rises while playing, so the swarm slowly falls into step with the music, and drifts apart again when stopped** | hits briefly brighten neighbours, tightening sync |
| **Flock** | Boids: soft points wheeling and splitting | cohesion, alignment, separation | the flock turns on the beat | kick scatters it; bands pull it toward their regions |
| **Pollen** | Particles carried on a slowly turning flow field | field scale, particle count, trail | gusts | bands set local wind; hats release new grains |
| **Comets** | Soft bodies orbiting unseen masses, with tails | mass count, gravity, tail length | perihelion on the downbeat | kicks fling them outward |

**Fireflies is the signature scene.** It shows the sync idea without one hard edge: no clock,
loose and organic; clock running, you watch order arrive.

### Pattern

| Scene  | The picture | Genes | Beat | Sound |
|--------|-------------|-------|------|-------|
| **Moiré** | Interference rings from two wandering point sources | wavelength, source distance, beat rate | the sources trade phase | bands detune the wavelengths |
| **Lissajous** | A glowing pendulum trace whose ratio slowly changes | ratio, phase, trail | one cycle per bar | the harmonic balance (low vs high) picks the ratio |
| **Julia** | A slice of a Julia set, its *c* wandering along the Mandelbrot boundary | c path speed, zoom, iteration colour | zoom breathes | loudness pushes *c* toward chaos; quiet lets it settle |
| **Cells** | Soap-bubble Voronoi cells drifting and pressing on each other | cell count, wall softness, drift | the cells pulse | each cell is tied to a band and swells with it |

**Kaleido** is not a scene. It is the FOLD gene (a four-way or eight-way mirror) that any
organic scene can grow into during drift. A held scene keeps whatever fold it has.

### Graphic

Pixel-sharp scenes drawn at pad resolution, with no supersampling. They still use the
drifting palettes, BLOOM trails and gene drift, so even a classic meter glows.

| Scene  | The picture | Genes | Beat | Sound |
|--------|-------------|-------|------|-------|
| **Bars** | The classic 8-band equalizer | peak hold, fall speed, gradient direction | peaks flash | column height is its band |
| **Mirror** | Bars growing from the centre row | fall speed, split, tilt | centre line pulses | bands, mirrored up and down |
| **Pulse** | The whole grid breathes | curve, edge falloff, hue swing | breath on every beat | loudness sets the floor; kick sets the peak |
| **Rings** | Squares spreading out from the centre | ring width, speed, origin wander | a ring on every beat | kick makes a bright ring, hats thin ones |
| **Scanner** | A column sweeping across | width, direction, echo | one column per 16th | bands light the column's height |
| **Radar** | A rotating arm with a fading wake | arm width, wake, blips | one turn per bar | hits leave blips |
| **Spiral** | A snake tracing a spiral | length, direction, start corner | one lap per bar | loudness lengthens it |
| **Checker** | A checkerboard flipping | cell size (1, 2 or 4 pads), flip pattern | flips on beats | brightness from loudness |
| **Tunnel** | Squares zooming toward you | speed, twist, depth fade | a new square per beat | bass speeds the zoom |
| **Life** | Conway's Game of Life | birth colour, step rate, reseed threshold | one generation per 8th | hits seed gliders; reseeds itself when it dies |
| **Steps** | The 16-step position as 4x4 blocks | trail, fill, order | the step light | hits flash the current block |
| **Sparkle** | Random glints on a dark field | density, decay, colour spread | glints cluster on beats | hats make glints, kick a burst |

---

## Evolution: the show never sits still

Three layers work at three timescales.

**1. Genes (seconds to minutes).** Each gene wanders on an **Ornstein-Uhlenbeck** walk: a
random walk pulled gently back to its home value. It drifts, but it never wanders off into
ugliness. The walk's speed is set by DRIFT; its home is set by the Look knobs. The same scene
at minute 1 and minute 6 is clearly related and clearly different. DRIFT at 0 freezes the
genes, for a performer who wants control.

**2. Palette drift (bars to minutes).** Every COLOR family is a cosine palette, colour(t) = a
+ b·cos(2π(c·t + d)), as in Quilez. Viz animates:

- the phase *d*: it creeps continuously and steps a little on each bar;
- the warmth: the energy arc rotates *d* toward warm when dense and toward cool when sparse.

The family never changes by itself; only its mood moves.

**3. Journey (phrases to a whole set).** SCENE's first three values are **Journey**,
**Journey: Organic** and **Journey: Graphic**. *Journey* is the default.

- **When to change:**
  - Move playing: at the end of a phrase (CYCLE bars), counted from the transport start.
  - Stopped: on a section change, or after about two minutes.
- **What comes next.** Each scene carries a calm-to-busy rating. Viz picks the next scene so
  that the rating matches the energy arc, with a little randomness and no immediate repeats:
  - calm music draws **Sky** and **Water**;
  - busier music draws **Growth**, **Swarm** and **Fluid**;
  - the densest, most rhythmic moments draw **Graphic** (plain *Journey* only).
  So a breakdown drifts to Aurora, and the drop arrives as Ink or Rings.
- **How it changes: a dissolve, never a cut.** For the change, Viz renders both scenes and
  mixes them through a moving noise-threshold mask over 1–4 bars, so one world seeps into
  the other along organic edges. Graphic-to-Graphic changes may cut on the downbeat; that
  suits them.
- **Picking a named scene holds it.** The genes and palette still drift.
- **Dreaming.** When Move is stopped and the mix is silent for a few seconds, Viz settles
  into a dim, slowly breathing version of the current scene. It **never goes black**. Sound
  wakes it within one frame.

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

- **Organic scenes** render a **32x32 float field** (one value, or a value plus a colour
  offset, per sample). Simulations run on 16x16–32x32 grids as each scene needs. Each pad
  is the box average of its 4x4 samples, and BLOOM adds a small blur and frame-to-frame
  persistence.
- **Graphic scenes** render at 8x8 directly.
- The **underglow** for each side is the average of that side's outer two rows of the
  field. With EDGE set to Breathe, the beat drives it.
- **Values to colour:** value → COLOR gradient → linear RGB → LIGHT and gamma.

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
  rotation. This heals pads that Move's own MIDI out painted over (measured: drum notes
  36–38 lit the bottom-left pads), and any light the Matrix missed, for example across a
  replug. There is no hotplug signal, and packets sent with nothing attached simply go
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

### TURN

TURN rotates the picture in 90° steps at the pad-mapping stage, so the show matches however
the Matrix sits beside the Move. Scenes and the underglow sides rotate together.

---

## Control surface

Two pages: page 1 holds the knobs on `root`, and one `{"level": "settings", "label":
"Settings"}` link adds page 2. A flat `chain_params` list declares every key, because without
it the Master FX cache reads only the first `params` array
(`src/host/shadow_chain_mgmt.c:1711-1740`). Wayward is the working example
(`wayward/src/dsp/wayward.c:1281-1335`). Schwung appends its own *My Presets* and *Module*
pages after these.

Every `short_name` is a real word of at most 5 characters, drawn verbatim (checked with
`labelVerbatim()`, `render_page_movy.mjs:311-346`). The full meaning is in `name`, which the
held-knob header shows.

### Page 1: Look

These shape the picture. Where a scene has genes, these knobs set the genes' **home**; DRIFT
decides how far the genes wander from it.

| Knob | Key | Label | Behaviour |
|------|-----|-------|-----------|
| 1 | `scene` | SCENE | **Scene.** Enum: *Journey*, *Journey: Organic*, *Journey: Graphic*, then the 36 scenes grouped by family. Turning it cross-dissolves to the new scene over one beat (or 0.5 s when stopped). Default *Journey*. |
| 2 | `color` | COLOR | **Colour family.** Enum: Dusk, Lagoon, Ember, Moss, Orchid, Aurora, Ink, Prism, Bone, Neon. Each is a cosine palette whose mood drifts (see Evolution). Default Dusk. |
| 3 | `flow` | FLOW | **Flow speed.** How fast things move. With the clock running it is tempo-relative, from ¼× to 4× in musical steps around 1×. When stopped it is free, at a matching rate. Default centre. |
| 4 | `grow` | GROW | **Detail.** Complexity: noise octaves, particle and agent counts, cell count, zoom. Low is a few large soft shapes; high is fine, busy texture. |
| 5 | `bloom` | BLOOM | **Glow and trails.** Softness (blur) and how long light lingers (persistence). Low is crisp and immediate; high is dreamy smears. |
| 6 | `react` | REACT | **Reaction.** How strongly the music pushes the picture: the spring depth for hits, bands and breathing. 0 is a pure ambient piece; high slams. |
| 7 | `warp` | WARP | **Warp.** Turbulence and distortion: domain-warp depth, curl strength, flock jitter, wave nonlinearity. On Graphic scenes it bends lines into curves. |
| 8 | `drift` | DRIFT | **Evolution speed.** How fast the genes wander and the palette mood moves. 0 holds the scene exactly as set. Journey timing is CYCLE's job, not this knob's. |

### Page 2: Settings

| Knob | Key | Label | Behaviour |
|------|-----|-------|-----------|
| 1 | `mode` | MODE | **Sync mode.** Enum: Auto / Beat / Sound / Blend (see the table under *Mode*). Default Auto. |
| 2 | `power` | POWER | **Power source.** Enum: Port / Hub. *Port* (default) keeps every frame inside what the Move's USB-A port can supply (measured; see *The power guard*). *Hub* removes the limit, for a Matrix on a powered USB hub. |
| 3 | `light` | LIGHT | **Brightness.** Overall level, 0–100%, applied after the power guard, so it can only make things dimmer. Default 80%. |
| 4 | `grid` | GRID | **Grid device.** Enum: Matrix / LP Mini MK3 / LP X / LP Pro MK3 / LP MK2 / LP Pro (see *Other grids*). Default Matrix. Launchpads are untested. |
| 5 | `cycle` | CYCLE | **Journey phrase.** Enum: 4 / 8 / 16 / 32 / 64 bars between Journey scene changes while Move plays. Default 16. |
| 6 | `turn` | TURN | **Rotation.** Enum: 0° / 90° / 180° / 270°. Default 0°. |
| 7 | `edge` | EDGE | **Underglow.** Enum: Off / Glow (each side carries the picture's colour beside it) / Breathe (the beat pulses the whole ring). Default Glow. |
| 8 | `nudge` | NUDGE | **Light timing.** −150 to +100 ms. It shifts the lights earlier or later to match the sound you hear. **Default −40 ms**: driven straight off the clock, the lights ran about 40 ms ahead of the speakers (probe Beat test). |

---

## Rejected alternatives

- **Icon scenes as the main identity.** The first draft's set (bars, checker, radar,
  scanner…) was judged not beautiful enough to lead. It remains as the Graphic family, with
  the organic families leading.
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
- **Hard cuts between scenes.** Replaced by the dissolve (Graphic-to-Graphic excepted).
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
- **Scene state:** one union of fixed-size scene structs per running scene, two in total
  (current plus incoming, for the dissolve). A new scene is initialised by the render
  thread, never by `set_param`.
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
  - turn off Move's MIDI output to USB-A for a clean picture;
  - turn on Move→Schwung so Viz hears Move's own instruments, not only Schwung modules;
  - LIGHT 0 before removing Viz, or the last picture stays;
  - Launchpads: pick GRID; untested;
  - what each knob does.

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
   - Organic scenes: Aurora, Nebula, Tide, Fireflies, Reeds, Lava.
   - Graphic scenes: Bars, Pulse, Scanner.
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
- TURN rotates a known test frame correctly.

**Listening**
- A sine sweep raises the bands in order.
- A kick loop fires low hits on the kicks and no high hits.
- Silence reaches dreaming and never black.
- With the clock stopped, a 120 BPM kick loop gives an own beat that phase-locks within 4
  beats.

**Scenes**
- Every scene runs for 10 simulated minutes under music and under silence. Every field value
  stays finite (no NaN or inf), and no scene is all-black for more than 2 s under music.
- Frames are bit-identical from a fixed seed.
- Genes stay within their bounds over 1 simulated hour at DRIFT max.
- A Journey dissolve always reaches the next scene, and never picks the same scene twice in
  a row.
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
