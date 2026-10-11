# Viz

A Schwung Master FX for Ableton Move that turns a 203 Systems Matrix (Mystrix) into a
light show for whatever the Move is playing. Plug the Matrix into the Move's USB-A port,
open its Performance app, and Viz paints the 8x8 grid and its underglow: organic,
abstract pictures that breathe with Move's clock while it plays, follow the sound of the
mix when it stops, and keep evolving over a whole song.

Launchpads (Mini MK3, X, Pro MK3, MK2, Pro) are offered through the GRID setting, untested.

**Status:** 0.1.0 in progress: the light engine, the listening, the device layer, and the
first nine scenes (Aurora, Nebula, Tide, Fireflies, Reeds, Lava, Bars, Pulse, Scanner).
See [DESIGN.md](DESIGN.md).

- `bash tests/run.sh` runs the tests (needs a Schwung checkout at `../schwung`, or `SCHWUNG=`).
- `build/preview --out page.html` renders every scene against a demo track into one page
  (`--wav file.wav` for your own 16-bit 44.1 kHz audio).
- `scripts/build.sh` cross-compiles for the Move; `scripts/install.sh` copies it there.
