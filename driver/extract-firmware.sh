#!/bin/sh
# extract-firmware.sh - copy the MB86M01 controller firmware out of a
# StationTV package or an installed StationTV.app into ./firmware/.
#
# The firmware is PIXELA's proprietary blob and is NOT redistributed in this
# repository. Run this against a copy you obtained legitimately.
#
# Usage:
#   ./extract-firmware.sh /path/to/StationTV.pkg
#   ./extract-firmware.sh "/Applications/StationTV.app"
#   ./extract-firmware.sh            # search common install locations
set -eu

DEST="$(dirname "$0")/firmware"
REL="Contents/Frameworks/DT195Lib.framework/Versions/A/Resources"
# DT195Lib is nested deep inside the server app bundle:
NEST="Contents/Resources/StationTV Helper.app/Contents/Resources/StationTV Server.app/Contents/Frameworks/AirTunerSystemFelis.framework/Versions/A/Frameworks/DT195Lib.framework/Versions/A/Resources"

FILES="mb86m01_assp_osec_idle.bin mb86m01_assp_osec_trc_m_h.bin mb86m01_assp_osec_trc_h_h.bin"

copy_from_dir() {
    src="$1"
    found=0
    for f in $FILES; do
        if [ -f "$src/$f" ]; then
            cp "$src/$f" "$DEST/$f"
            echo "  $f"
            found=1
        fi
    done
    return $([ "$found" = 1 ] && echo 0 || echo 1)
}

find_in_tree() {
    root="$1"
    dir=$(find "$root" -type d -name Resources \
        -path '*DT195Lib.framework*' 2>/dev/null | head -1)
    [ -n "$dir" ] && copy_from_dir "$dir"
}

mkdir -p "$DEST"

if [ $# -ge 1 ]; then
    SRC="$1"
    case "$SRC" in
    *.pkg)
        TMP=$(mktemp -d)
        trap 'rm -rf "$TMP"' EXIT
        echo "Expanding $SRC ..."
        if command -v pkgutil >/dev/null 2>&1; then
            pkgutil --expand-full "$SRC" "$TMP/pkg" 2>/dev/null || \
                pkgutil --expand "$SRC" "$TMP/pkg"
        else
            echo "pkgutil not found; expand the .pkg manually first." >&2
            exit 1
        fi
        find_in_tree "$TMP/pkg" && exit 0
        echo "firmware not found inside the package" >&2
        exit 1
        ;;
    *)
        if find_in_tree "$SRC"; then exit 0; fi
        echo "firmware not found under $SRC" >&2
        exit 1
        ;;
    esac
fi

# No argument: probe common install locations.
for app in "/Applications/StationTV.app" "$HOME/Applications/StationTV.app"; do
    if [ -d "$app" ] && find_in_tree "$app"; then
        echo "Extracted to $DEST"
        exit 0
    fi
done

echo "Could not find StationTV; pass a .pkg or .app path." >&2
exit 1
