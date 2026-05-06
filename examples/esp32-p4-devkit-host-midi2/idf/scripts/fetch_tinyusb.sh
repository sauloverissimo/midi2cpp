#!/usr/bin/env bash
# fetch_tinyusb.sh, one-off bootstrap.
#
# Clones the TinyUSB PR #3571 fork into idf/external/tinyusb at a
# pinned SHA. The shim component at idf/components/tinyusb registers a
# subset of the fork's source files as an ESP-IDF component, so the
# fork lives outside components/ to avoid name collisions with the
# component manager.
#
# Re-running this script is safe; it only touches external/tinyusb.
# If you have a working copy of the fork on disk, symlink it manually:
#   ln -sfn /path/to/your/tinyusb idf/external/tinyusb

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
IDF_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
TARGET="${IDF_DIR}/external/tinyusb"

# Pinned to the experiment/midi-coexistence branch tip on the fork.
# Stack of follow-ups on top of the PR #3571 base (31d730d8):
#   91a54581 - alt-walk bcdMSC defer that lets CFG_TUH_MIDI and
#              CFG_TUH_MIDI2 coexist (used by the dual-stack bridge
#              sibling at ../../esp32-p4-devkit-bridge-midi2/).
#   b336ce0d - opt-in user responder via CFG_TUD_MIDI2_USER_RESPONDER
#              (device-side feature, no effect on this host-only
#              build, kept in lockstep with the bridge sibling because
#              the bridge symlinks this clone).
# The added code is fully gated by `#if CFG_TUH_MIDI2`,
# `#if !CFG_TUH_MIDI2_LEGACY_FALLBACK`, and
# `#if CFG_TUD_MIDI2_USER_RESPONDER`, so a host-only build that leaves
# those flags at their defaults compiles to byte-identical output as
# the PR base.
TINYUSB_REPO="https://github.com/sauloverissimo/tinyusb.git"
TINYUSB_SHA="b336ce0dab66b44b7cad686a6473c02010c22aaf"

mkdir -p "${IDF_DIR}/external"

if [[ -d "${TARGET}/.git" ]]; then
    echo "[fetch_tinyusb] external/tinyusb already cloned, fetching pinned SHA"
    git -C "${TARGET}" fetch --depth=1 origin "${TINYUSB_SHA}" 2>/dev/null || \
        git -C "${TARGET}" fetch origin
    git -C "${TARGET}" checkout -q "${TINYUSB_SHA}"
else
    echo "[fetch_tinyusb] cloning ${TINYUSB_REPO} into external/tinyusb"
    rm -rf "${TARGET}"
    git clone --filter=tree:0 "${TINYUSB_REPO}" "${TARGET}"
    git -C "${TARGET}" checkout -q "${TINYUSB_SHA}"
fi

echo "[fetch_tinyusb] external/tinyusb is at SHA ${TINYUSB_SHA}"
echo "[fetch_tinyusb] done"
