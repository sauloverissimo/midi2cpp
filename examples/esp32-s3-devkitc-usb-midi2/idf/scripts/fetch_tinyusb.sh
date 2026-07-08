#!/usr/bin/env bash
# fetch_tinyusb.sh, one-off bootstrap.
#
# Clones TinyUSB upstream (PR #3738, merged) into idf/external/tinyusb.
# The shim component at idf/components/tinyusb registers a subset of
# the source files as an ESP-IDF component, kept outside components/
# to avoid name collisions with the component manager.
#
# Re-running this script is safe; it only touches external/tinyusb.
# If you have a working copy on disk, symlink it manually:
#   ln -sfn /path/to/your/tinyusb idf/external/tinyusb

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
IDF_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
TARGET="${IDF_DIR}/external/tinyusb"

# Tracks the same ref as the Pico SDK recipes. Update in lockstep.
TINYUSB_REPO="https://github.com/hathach/tinyusb.git"
TINYUSB_REF="536157cb98fbc40329c9695281506fe7f04e526f"

mkdir -p "${IDF_DIR}/external"

if [[ -d "${TARGET}/.git" ]]; then
    echo "[fetch_tinyusb] external/tinyusb already cloned, fetching pinned ref"
    git -C "${TARGET}" fetch --depth=1 origin "${TINYUSB_REF}" 2>/dev/null || \
        git -C "${TARGET}" fetch origin
    git -C "${TARGET}" checkout -q "${TINYUSB_REF}"
else
    echo "[fetch_tinyusb] cloning ${TINYUSB_REPO} into external/tinyusb"
    rm -rf "${TARGET}"
    git clone --filter=tree:0 "${TINYUSB_REPO}" "${TARGET}"
    git -C "${TARGET}" checkout -q "${TINYUSB_REF}"
fi

echo "[fetch_tinyusb] external/tinyusb is at ref ${TINYUSB_REF}"
echo "[fetch_tinyusb] done"
