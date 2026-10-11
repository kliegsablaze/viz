# Viz

A Schwung Master FX for Ableton Move that turns a grid controller into a light show for
whatever the Move is playing. Plug a 203 Systems Matrix (Mystrix) into the Move's USB-A
port, open its Performance app, and load Viz into a Master FX position. Viz paints the 8x8
grid and its underglow with bold, geometric scenes that step on Move's clock while it plays
and follow the sound of the mix when it stops. It leaves the audio untouched.

Launchpads (Mini MK3, X, Pro MK3, MK2, Pro) are offered through the GRID setting, untested.

**Status:** 0.2.0, the first release. See [DESIGN.md](DESIGN.md).

## What you get

- **106 scenes:** 100 geometric ones (wipes, rings, sprites, little games and toys) and six
  soft, organic extras.
- **Look:** SCENE, COLOR, RATE, SIZE, TRAIL, FOLD, SHIFT, REACT. Every knob changes the
  picture at once, so you can play it.
- **SEQ:** an eight-step sequence of scenes, each step with its own length, colours, rate,
  size and fold, any of them Random.
- **MOD:** two LFOs that move any Look knob, on the beat, on the kick or on the level.
- **Settings:** MODE (Auto, Beat, Sound, Blend), power source, brightness, grid device,
  rotation, underglow and light timing.
- **The Remote UI:** open `http://move.local:7700` → Remote UI → Master FX to watch the
  8x8 picture in a browser, or pop it out full screen.
- **Module Help** on the Move, one jog from the controls.

## Good to know

- **Hearing Move's own tracks:** a Master FX hears Schwung's instruments always, and
  Move's own tracks only with Move→Schwung turned on in Schwung's Global Settings. MODE
  Beat (the default) needs no sound at all: it follows Move's clock.
- **Power:** the Move's USB-A port can't power a bright Matrix. POWER Port (the default)
  keeps every frame within what the port can give; use a powered hub for full brightness.

## Building

- `bash tests/run.sh` runs the tests (needs a Schwung checkout at `../schwung`, or
  `SCHWUNG=`).
- `build/preview --out page.html` renders every scene against a demo track into one page
  (`--wav file.wav` for your own 16-bit 44.1 kHz audio).
- `tools/gallery/gallery.html` is the scene gallery, the source every scene is drawn in
  first.
- `scripts/build.sh` cross-compiles for the Move; `scripts/install.sh` copies it there.
- `scripts/release.sh` tags a release (see DESIGN.md, *Releasing*).

## License

[PolyForm Strict 1.0.0](LICENSE): free to use, not to copy, change or redistribute.
