#!/usr/bin/env bash
# Build nvs.bin from tools/nvs_keys.csv and write it to the NVS partition.
#
# Usage:
#   tools/flash_nvs.sh [/dev/ttyACM0]
#
# Prerequisites:
#   - IDF environment sourced (`. $HOME/esp/esp-idf/export.sh`) so $IDF_PATH and
#     esptool.py are on PATH.
#   - tools/nvs_keys.csv has been edited with real credentials.
#
# This writes ONLY the NVS partition (0x9000, 0x5000 bytes per partitions.csv).
# It does NOT touch bootloader, partition table, or the app image.

set -euo pipefail

PORT="${1:-/dev/ttyACM0}"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/.." && pwd)"
CSV="$HERE/nvs_keys.csv"
BIN="$HERE/nvs.bin"
NVS_OFFSET="0x9000"
NVS_SIZE="0x5000"

if [[ -z "${IDF_PATH:-}" ]]; then
    echo "error: IDF_PATH not set. Source export.sh first:" >&2
    echo "    . \$HOME/esp/esp-idf/export.sh" >&2
    exit 2
fi

GEN="$IDF_PATH/components/nvs_flash/nvs_partition_generator/nvs_partition_gen.py"
if [[ ! -f "$GEN" ]]; then
    echo "error: nvs_partition_gen.py not found at $GEN" >&2
    exit 2
fi

if [[ ! -f "$CSV" ]]; then
    cat >&2 <<EOF
error: $CSV missing.

The committed template is tools/nvs_keys.csv.example. Copy it locally:

    cp tools/nvs_keys.csv.example tools/nvs_keys.csv

then edit tools/nvs_keys.csv with your real WiFi creds + bearer token.
(tools/nvs_keys.csv is gitignored so secrets never get committed.)
EOF
    exit 2
fi

echo "[1/2] Generating NVS image: $BIN"
python3 "$GEN" generate "$CSV" "$BIN" "$NVS_SIZE"

echo "[2/2] Writing $BIN -> $PORT at $NVS_OFFSET"
esptool.py -p "$PORT" -b 460800 write_flash "$NVS_OFFSET" "$BIN"

echo "Done. The device's NVS partition now holds the keys in $CSV."
echo "Re-flashing the app does NOT clear NVS; only \`idf.py erase-flash\` does."
