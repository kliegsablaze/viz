#!/usr/bin/env bash
# Builds the probe in the wayward-builder image (aarch64 cross gcc) and
# optionally installs it on the Move. Usage: scripts/build.sh [--install]
set -euo pipefail
cd "$(dirname "$0")/.."
rm -rf dist && mkdir -p dist/viz-probe
docker run --rm -v "$PWD:/build" -w /build wayward-builder \
    aarch64-linux-gnu-gcc -O2 -Wall -Wextra -Werror -shared -fPIC \
    src/dsp/viz_probe.c -Isrc/dsp -o dist/viz-probe/viz-probe.so -lm -lpthread -ldl
cp src/module.json dist/viz-probe/
if [ "${1:-}" = "--install" ]; then
    D=/data/UserData/schwung/modules/audio_fx/viz-probe
    ssh ableton@move.local "mkdir -p $D"
    scp -q dist/viz-probe/* "ableton@move.local:$D/"
    echo "installed to $D"
fi
