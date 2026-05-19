#!/bin/bash
### Build -> flash app -> (optional) flash NVS provisioning -> monitor
###
### Usage:
###   ./flash.sh                # full flow, monitor after
###   ./flash.sh --no-monitor   # skip the serial monitor step
###   ./flash.sh --app-only     # only flash app image, skip NVS
###   ./flash.sh -h             # this help
###
### NVS provisioning:
###   If tools/nvs_keys.csv exists, it's compiled with the IDF NVS partition
###   generator and written to offset 0x9000 right after the app flash. The
###   committed template is tools/nvs_keys.csv.example — copy it locally and
###   edit your real WiFi/bearer secrets there (the local copy is gitignored).
###
### Pre-reqs:
###   - ESP-IDF installed and exported (we source export.sh below).
###   - Board attached. On WSL: `usbipd wsl attach -a -b [BUSID]` from PowerShell.

set -euo pipefail

# Configs
IDF_PATH=${IDF_PATH:-$HOME/esp/esp-idf}
SERIAL_PORT=${SERIAL_PORT:-/dev/ttyACM0}
DO_MONITOR=1
DO_NVS=1

help() {
    sed -rn 's/^### ?//;T;p' "$0"
}

for arg in "$@"; do
    case "$arg" in
        -h|--help)        help; exit 0 ;;
        --no-monitor)     DO_MONITOR=0 ;;
        --app-only)       DO_NVS=0 ;;
        *) echo "unknown arg: $arg"; help; exit 2 ;;
    esac
done

# Resolve IDF if the path the user passed doesn't exist
if [[ ! -f "${IDF_PATH}/export.sh" ]]; then
    for alt in "$HOME/esp/esp-idf" "$HOME/esp/esp-idf-v4.4.6"; do
        if [[ -f "$alt/export.sh" ]]; then IDF_PATH="$alt"; break; fi
    done
fi
if [[ ! -f "${IDF_PATH}/export.sh" ]]; then
    echo "error: ESP-IDF not found. Set IDF_PATH or install at \$HOME/esp/esp-idf." >&2
    exit 2
fi

# Pick up the IDF Python venv before sourcing export.sh, otherwise idf.py
# falls back to the wrong python and the requirements check fails.
IDF_PYTHON_ENV_PATH=${IDF_PYTHON_ENV_PATH:-$HOME/.espressif/python_env/idf4.4_py3.8_env}
if [[ -d "$IDF_PYTHON_ENV_PATH/bin" ]]; then
    export IDF_PYTHON_ENV_PATH
    export PATH="$IDF_PYTHON_ENV_PATH/bin:$PATH"
fi

# Source IDF
. "${IDF_PATH}/export.sh" > /dev/null

# 0) Component drift guard — vendored repos in components/ tend to wander
# forward as IDEs/auto-fetch update them, and dev-main's app code targets
# specific API surfaces (mooncake v1.x, pre-v2 rewrite; M5GFX/M5Unified
# 0.2.0 pair). Snap everything back to the recorded SHAs before build.
if [[ -x tools/pin_components.sh ]]; then
    echo "==> Checking vendored component pins"
    tools/pin_components.sh
fi

# 1) Build + flash app
idf.py -p "${SERIAL_PORT}" flash -b 1500000

# 2) Optional NVS partition flash
if [[ $DO_NVS -eq 1 ]]; then
    if [[ -f tools/nvs_keys.csv ]]; then
        echo ""
        echo "==> Flashing NVS partition from tools/nvs_keys.csv"
        tools/flash_nvs.sh "${SERIAL_PORT}"
    else
        cat <<EOF

==> Skipping NVS flash: tools/nvs_keys.csv not present.
    To pre-provision WiFi creds + Claude bearer token at flash time:
        cp tools/nvs_keys.csv.example tools/nvs_keys.csv
        # edit tools/nvs_keys.csv with your values
        ./flash.sh

    (You can also enter them at runtime via Set WiFi / Claude Meter > E.)

EOF
    fi
fi

# 3) Monitor
if [[ $DO_MONITOR -eq 1 ]]; then
    idf.py -p "${SERIAL_PORT}" monitor
fi
