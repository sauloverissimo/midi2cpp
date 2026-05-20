#!/usr/bin/env bash
# fetch_tinyusb.sh, one-off bootstrap.
#
# Clones the TinyUSB experiment/midi-coexistence branch into
# idf/external/tinyusb at a pinned SHA. The shim component at
# idf/components/tinyusb registers a subset of the source files as an
# ESP-IDF component, kept outside components/ to avoid name collisions
# with the component manager.
#
# Re-running this script is safe; it only touches external/tinyusb.
# If you have a working copy on disk, symlink it manually:
#   ln -sfn /path/to/your/tinyusb idf/external/tinyusb

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
IDF_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
TARGET="${IDF_DIR}/external/tinyusb"

# Pinned to the experiment/midi-coexistence branch tip on the fork.
# Stack of two follow-ups on top of TinyUSB upstream master (which
# already includes the MIDI 2.0 driver merged via PR #3571):
#   alt-walk bcdMSC defer that lets CFG_TUH_MIDI and CFG_TUH_MIDI2
#   coexist (used by the dual-stack bridge sibling at
#   ../../esp32-p4-devkit-bridge-midi2/), and an opt-in user responder
#   via CFG_TUD_MIDI2_USER_RESPONDER (device-side feature, no effect
#   on this host-only build, kept in lockstep with the bridge sibling
#   because the bridge symlinks this clone).
# The added code is fully gated by `#if CFG_TUH_MIDI2`,
# `#if !CFG_TUH_MIDI2_LEGACY_FALLBACK`, and
# `#if CFG_TUD_MIDI2_USER_RESPONDER`, so a host-only build that leaves
# those flags at their defaults compiles to byte-identical output as
# upstream master.
TINYUSB_REPO="https://github.com/sauloverissimo/tinyusb.git"
TINYUSB_SHA="490a056eeb1fc1a835a1f0247c49d646ff8f5d5e"

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
