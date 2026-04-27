#!/usr/bin/env bash
# Vendor midi2 C99 amalgamation from upstream clone.
# Usage: ./scripts/vendor_midi2.sh [PATH_TO_MIDI2_REPO]
set -euo pipefail

UPSTREAM="${1:-../repositories/midi2}"

if [ ! -f "$UPSTREAM/dist/midi2.h" ]; then
    echo "error: midi2.h not found at $UPSTREAM/dist/"
    echo "usage: $0 [path-to-midi2-repo]"
    exit 1
fi

mkdir -p src
cp "$UPSTREAM/dist/midi2.h" src/midi2.h
cp "$UPSTREAM/dist/midi2.c" src/midi2.c

VERSION=$(grep "Auto-generated from midi2" src/midi2.h | head -1)
echo "Vendored: $VERSION"
