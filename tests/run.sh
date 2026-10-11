#!/usr/bin/env bash
# Builds Viz natively, drives it black-box through the v2 API, plans its pages
# with the host's own planner, lints the help, and builds the preview tool.
#   SCHWUNG=<checkout> bash tests/run.sh
set -euo pipefail

cd "$(dirname "$0")/.."

SCHWUNG="${SCHWUNG:-../schwung}"
out="build/tests"
mkdir -p "$out"
CFLAGS="-std=c11 -O2 -Wall -Wextra -Werror -D_DEFAULT_SOURCE -Isrc/dsp"

cc $CFLAGS tests/test_viz.c src/dsp/*.c -lm -lpthread -o "$out/test_viz"
cc $CFLAGS tools/preview/preview.c src/dsp/features.c src/dsp/render.c src/dsp/scenes.c -lm -lz -o build/preview

rc=0
"$out/test_viz" "$out" || rc=$?

if command -v node >/dev/null 2>&1; then
  node tests/plan.test.mjs "$out" "$SCHWUNG" || rc=$?
  node tests/help_lint.mjs || rc=$?
else
  echo "FAIL: node is not on PATH (tests/plan.test.mjs, tests/help_lint.mjs)"
  rc=1
fi

build/preview --seconds 4 --out "$out/preview.html" 2>/dev/null || { echo "FAIL: preview"; rc=1; }

exit $rc
