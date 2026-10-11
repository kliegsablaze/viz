#!/usr/bin/env bash
# Builds viz.so for aarch64 (Ableton Move) into dist/viz/ and packages
# dist/viz-module.tar.gz. Runs the cross compiler from the fleet's builder
# image unless it is on PATH.
set -euo pipefail

cd "$(dirname "$0")/.."

IMAGE="${VIZ_BUILD_IMAGE:-wayward-builder}"
CC_CMD="aarch64-linux-gnu-gcc -std=c11 -D_DEFAULT_SOURCE -ffp-contract=off -g -O2 -shared -fPIC -Wall -Wextra -Werror \
  -Isrc/dsp src/dsp/*.c -o dist/viz/viz.so -lm -lpthread -ldl"

rm -rf dist
mkdir -p dist/viz

if command -v aarch64-linux-gnu-gcc >/dev/null 2>&1; then
    sh -c "$CC_CMD"
else
    docker run --rm -v "$PWD:/build" -w /build "$IMAGE" sh -c "$CC_CMD"
fi

# help.json is what puts "Module Help" one jog from the controls.
cp src/module.json src/help.json LICENSE dist/viz/
echo "Built dist/viz/"

cd dist
tar -czf viz-module.tar.gz viz/
cd ..
echo "Tarball: dist/viz-module.tar.gz"
