#!/usr/bin/env bash
# Screenshot the application on a throwaway X display.
#
# Xvfb rather than the real session: the capture is deterministic (fixed size, no wallpaper, no
# panel), it does not disturb whatever is on screen, and it works over SSH and in CI. The cost is
# XWayland-style rendering rather than native Wayland, which for a screenshot of our own widgets is
# no difference at all.
#
#   tools/shoot.sh <out.png> [width] [height] [seconds-to-settle]
#
# Environment:
#   TS_FILE   a MIDI file to open on launch
#   TS_ROM    a SCCore.dll to pre-install into the throwaway profile, so the player screen is
#             reachable instead of the first-run one

set -euo pipefail
cd "$(dirname "$0")/.."

OUT="${1:?usage: shoot.sh <out.png> [width] [height] [settle]}"
WIDTH="${2:-720}"
HEIGHT="${3:-820}"
SETTLE="${4:-4}"

BIN="build/dev/src/tabula-sonora-player"
SCHEMAS="build/dev/data"
APP_ID="co.losno.TabulaSonoraPlayer"

if [ ! -x "$BIN" ]; then
    echo "Build first: cmake --build --preset dev" >&2
    exit 1
fi

DISPLAY_NUM=$(( (RANDOM % 100) + 90 ))
PROFILE="$(mktemp -d)"
trap 'kill "${XVFB_PID:-}" "${APP_PID:-}" 2>/dev/null || true; rm -rf "$PROFILE"' EXIT

Xvfb ":$DISPLAY_NUM" -screen 0 "${WIDTH}x${HEIGHT}x24" -nolisten tcp &
XVFB_PID=$!
sleep 1

# An isolated profile, so a run never inherits (or clobbers) real settings and always starts from a
# known screen.
export XDG_DATA_HOME="$PROFILE/data"
export XDG_CONFIG_HOME="$PROFILE/config"
export XDG_CACHE_HOME="$PROFILE/cache"
mkdir -p "$XDG_DATA_HOME" "$XDG_CONFIG_HOME" "$XDG_CACHE_HOME"

if [ -n "${TS_ROM:-}" ]; then
    mkdir -p "$XDG_DATA_HOME/tabula-sonora"
    cp "$TS_ROM" "$XDG_DATA_HOME/tabula-sonora/SCCore.dll"
fi

# The app's own icons, so the launcher and status-page marks resolve on a bare display.
mkdir -p "$XDG_DATA_HOME/icons"
cp -r data/icons/hicolor "$XDG_DATA_HOME/icons/" 2>/dev/null || true
gtk4-update-icon-cache -q -t -f "$XDG_DATA_HOME/icons/hicolor" 2>/dev/null || true

# An array, not a word-split string: the demo files have spaces in their names.
APP_ARGS=()
[ -n "${TS_FILE:-}" ] && APP_ARGS+=("$TS_FILE")

DISPLAY=":$DISPLAY_NUM" GDK_BACKEND=x11 GSETTINGS_SCHEMA_DIR="$SCHEMAS" \
    ADW_DEBUG_COLOR_SCHEME="${ADW_DEBUG_COLOR_SCHEME:-prefer-dark}" \
    "$BIN" "${APP_ARGS[@]}" &
APP_PID=$!

sleep "$SETTLE"

# Optionally open a dialog before capturing. GtkApplication exports its app-level actions on the
# session bus, which is the only way to reach one without synthesising a click.
if [ -n "${TS_ACTION:-}" ]; then
    gdbus call --session --dest "$APP_ID" \
        --object-path "/${APP_ID//.//}" \
        --method org.freedesktop.Application.ActivateAction \
        "$TS_ACTION" '[]' '{}' >/dev/null 2>&1 || echo "Could not activate $TS_ACTION" >&2
    sleep 2
fi

DISPLAY=":$DISPLAY_NUM" import -window root "$OUT"
echo "Wrote $OUT"
