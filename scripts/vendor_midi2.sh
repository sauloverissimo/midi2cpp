#!/usr/bin/env bash
# Vendor the midi2 C99 amalgam into src/ from a midi2 release tag.
# Usage: scripts/vendor_midi2.sh v0.6.0
set -euo pipefail
TAG="${1:?usage: vendor_midi2.sh <midi2-tag, e.g. v0.6.0>}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BASE="https://raw.githubusercontent.com/sauloverissimo/midi2/${TAG}/dist"
for f in midi2.h midi2.c; do
  curl -fsSL "${BASE}/${f}" -o "${ROOT}/src/${f}"
done
echo "Vendored midi2 ${TAG} -> src/midi2.{h,c}"
grep -m1 "Auto-generated from midi2 v" "${ROOT}/src/midi2.h" || true
