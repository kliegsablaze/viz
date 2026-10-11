#!/usr/bin/env bash
# Dev convenience: builds and copies Viz straight to a connected Move over
# SSH. Not part of the release pipeline.
set -euo pipefail

cd "$(dirname "$0")/.."

HOST="${MOVE_HOST:-ableton@move.local}"
D="/data/UserData/schwung/modules/audio_fx/viz"

./scripts/build.sh

# Refuse to ship a host build: an aarch64 ELF only.
if ! head -c 20 dist/viz/viz.so | od -An -tx1 | tr -d ' \n' | grep -Eq '^7f454c46.{28}b700'; then
    echo "dist/viz/viz.so is not an aarch64 ELF" >&2
    exit 1
fi

# Copy beside, then rename. Never scp onto a live path: the shim has the .so
# mapped while Viz sits in a slot, and rewriting it in place crashes Move.
ssh "$HOST" "mkdir -p '$D/.new'"
scp -q dist/viz/viz.so dist/viz/module.json dist/viz/help.json "$HOST:$D/.new/"
ssh "$HOST" "cd '$D' && chmod 755 .new/viz.so && for f in .new/*; do mv -f \"\$f\" .; done && rmdir .new"
echo "Installed to $HOST:$D"
echo "A Viz already loaded keeps running the old code until it is reloaded."
