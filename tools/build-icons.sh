#!/usr/bin/env bash
# Regenerate the hicolor launcher icon set from the Icon Composer master.
#
# The master is the same 2048x2048 export the Apple build and the web favicons come from -- a
# full-bleed squircle with transparent corners over a vertical #FF9C37 -> #9161D0 gradient. It is
# not in this repository; point SRC at it and re-run when the artwork changes. The derived PNGs
# *are* committed, so neither the build nor a packager needs ImageMagick or the master.
#
# Alpha policy: transparent corners are kept. Unlike iOS and Android, freedesktop icons are never
# masked by the platform, so filling the corners would only put a squircle inside whatever shape
# the theme draws.

set -euo pipefail
cd "$(dirname "$0")/.."

SRC="${SRC:-ts-iOS-Default-1024@2x.png}"
APP_ID="co.losno.TabulaSonoraPlayer"
OUT="data/icons/hicolor"

if [ ! -f "$SRC" ]; then
    echo "No icon master at '$SRC'. Set SRC to point at it." >&2
    exit 1
fi

for size in 16 24 32 48 64 128 256 512; do
    dir="$OUT/${size}x${size}/apps"
    mkdir -p "$dir"

    # Resize in linear light and back to sRGB. Downscaling gamma-encoded sRGB darkens the result,
    # and this gradient makes that shift visible.
    if [ "$size" -le 48 ]; then
        # One 2048 -> 16 Lanczos pass reads mushy: the note glyph and the lid rings blur together.
        # Stepping via 128px and then sharpening keeps them separable.
        magick "$SRC" -colorspace RGB -filter Lanczos -resize 128x128 \
            -filter Lanczos -resize "${size}x${size}" -colorspace sRGB \
            -unsharp 0x0.5+1.0+0.005 -depth 8 -strip "$dir/${APP_ID}.png"
    else
        magick "$SRC" -colorspace RGB -filter Lanczos -resize "${size}x${size}" \
            -colorspace sRGB -depth 8 -strip "$dir/${APP_ID}.png"
    fi
done

echo "Wrote:"
find "$OUT" -name "${APP_ID}.png" | sort
