#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"

# Locate and source ESP-IDF if not already in env.
# Prefer the eim-installed activator; fall back to vendor export.sh layouts.
if ! command -v idf.py >/dev/null 2>&1 && [[ -z "${IDF_PATH:-}" ]]; then
    if [[ -f "$HOME/.espressif/tools/activate_idf_v6.0.sh" ]]; then
        # eim layout (this machine): vendor export.sh is broken — use -e mode.
        _saved_path="$PATH"
        while IFS= read -r line; do
            [[ -n "$line" ]] && export "$line"
        done < <(bash "$HOME/.espressif/tools/activate_idf_v6.0.sh" -e)
        export PATH="$PATH:$_saved_path"
        unset _saved_path
        idf.py()      { "$IDF_PYTHON_ENV_PATH/bin/python" "$IDF_PATH/tools/idf.py" "$@"; }
        esptool.py()  { "$IDF_PYTHON_ENV_PATH/bin/python" "$IDF_PATH/components/esptool_py/esptool/esptool.py" "$@"; }
    else
        for c in \
            "$HOME/.espressif/v6.0/esp-idf/export.sh" \
            "$HOME/esp/esp-idf/export.sh" \
            "/opt/esp-idf/export.sh"; do
            if [[ -f "$c" ]]; then
                # shellcheck disable=SC1090
                . "$c" >/dev/null 2>&1 && break
            fi
        done
    fi
fi
command -v idf.py >/dev/null 2>&1 || { echo "ESP-IDF not found"; exit 1; }

PORT="${ESP_PORT:-/dev/ttyACM0}"
idf.py set-target esp32c6
idf.py build
if [[ "${1:-}" != "--build-only" ]]; then
    idf.py -p "$PORT" flash monitor
fi
