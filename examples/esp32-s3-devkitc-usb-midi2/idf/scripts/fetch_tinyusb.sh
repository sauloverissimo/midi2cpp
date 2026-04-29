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

# Pinned to the same SHA as the Pico SDK recipes (see CMakeLists.txt of
# adafruit-feather-rp2040-host-midi2). Update both in lockstep.
TINYUSB_REPO="https://github.com/sauloverissimo/tinyusb.git"
TINYUSB_SHA="31d730d8bb0b5c0832c5490378a2a2dd60ab72aa"

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
