#!/usr/bin/env bash
# Builds Viz natively, drives it black-box through the v2 API, plans its pages
# with the host's own planner, lints the help, and builds the preview tool.
#   SCHWUNG=<checkout> bash tests/run.sh
set -euo pipefail

cd "$(dirname "$0")/.."

SCHWUNG="${SCHWUNG:-../schwung}"
out="build/tests"
mkdir -p "$out"
# -ffp-contract=off: no fused multiply-adds, so the C rounds as the gallery's
# JavaScript does (tests/test_parity.c), here and in scripts/build.sh.
CFLAGS="-std=c11 -O2 -Wall -Wextra -Werror -D_DEFAULT_SOURCE -ffp-contract=off -Isrc/dsp"

cc $CFLAGS tests/test_viz.c src/dsp/*.c -lm -lpthread -o "$out/test_viz"
cc $CFLAGS tools/preview/preview.c src/dsp/features.c src/dsp/render.c src/dsp/scenes.c src/dsp/shapes.c -lm -lz -o build/preview
cc $CFLAGS tests/test_parity.c src/dsp/shapes.c -lm -o "$out/test_parity"

rc=0
"$out/test_viz" "$out" || rc=$?

if command -v node >/dev/null 2>&1; then
  node tests/parity_dump.mjs && "$out/test_parity" build/parity_js.bin build/parity_names.txt || rc=$?
  node tests/plan.test.mjs "$out" "$SCHWUNG" || rc=$?
  node tests/widgets.test.mjs "$out" "$SCHWUNG" || rc=$?
  node tests/help_lint.mjs || rc=$?
else
  echo "FAIL: node is not on PATH (tests/plan.test.mjs, tests/help_lint.mjs)"
  rc=1
fi

build/preview --seconds 4 --out "$out/preview.html" 2>/dev/null || { echo "FAIL: preview"; rc=1; }

exit $rc
