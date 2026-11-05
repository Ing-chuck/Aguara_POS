#!/usr/bin/env bash
# Build the installer, or a patch, from a release build.
#
#   installer/build.sh BUILD_DIR PG_CLIENT_BIN [patch]
#
#   BUILD_DIR       a release build (its bin/ has aguara.exe)
#   PG_CLIENT_BIN   pgsql/bin of the 32-bit PostgreSQL 10 zip
#
# Needs Qt and MinGW on PATH. Without ISCC (Inno Setup 6) it leaves the staging
# folder ready and stops: set ISCC to its path if it isn't on PATH.
# Run it on a computer with the 32-bit Visual C++ 2013 runtime, or put
# msvcr120.dll and msvcp120.dll in installer/redist/.
set -euo pipefail

BUILD_DIR="${1:?release build folder}"
PG_CLIENT_BIN="${2:?pgsql/bin of the 32-bit PostgreSQL 10 zip}"
MODE="${3:-full}"

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
BIN="$BUILD_DIR/bin"
STAGE="$BUILD_DIR/stage"

[ -f "$BIN/aguara.exe" ] || { echo "no aguara.exe in $BIN"; exit 1; }

number() { sed -n "s/^#define VERSION_$1 //p" "$ROOT/src/interfaces/version.h" | tr -d '\r'; }
VERSION="$(number MAJOR).$(number MINOR).$(number PATCH)"
echo "== AguaraPOS $VERSION ($MODE)"

rm -rf "$STAGE"
mkdir -p "$STAGE/modules" "$STAGE/doc" "$STAGE/scripts"
cp "$BIN/aguara.exe" "$BIN/aguara_interfaces.dll" "$STAGE/"
cp "$BIN"/modules/*.dll "$STAGE/modules/"

# Qt, its plugins (SQL drivers, printing, SVG icons), Spanish and the MinGW runtime
windeployqt --compiler-runtime --no-opengl-sw --no-angle --no-system-d3d-compiler \
    --sql --printsupport --svg --translations es --dir "$STAGE" \
    "$STAGE/aguara.exe" "$STAGE/aguara_interfaces.dll" "$STAGE"/modules/*.dll

# The client of the PostgreSQL server
for dll in libpq.dll libssl-1_1.dll libcrypto-1_1.dll libintl-8.dll libiconv-2.dll; do
    cp "$PG_CLIENT_BIN/$dll" "$STAGE/"
done

# libpq and OpenSSL need the Visual C++ 2013 runtime: next to them, so a till
# doesn't need it installed. installer/redist/ first, then this computer's.
for dll in msvcr120.dll msvcp120.dll; do
    if [ -f "$HERE/redist/$dll" ]; then
        cp "$HERE/redist/$dll" "$STAGE/"
    elif [ -f "/c/Windows/SysWOW64/$dll" ]; then
        cp "/c/Windows/SysWOW64/$dll" "$STAGE/"
    else
        echo "Missing $dll (32-bit): put it in installer/redist/"; exit 1
    fi
done

cp "$ROOT"/doc/manual/*.pdf "$STAGE/doc/"
cp "$ROOT/doc/deploy/varios-puestos-postgresql.md" "$STAGE/doc/"
cp "$ROOT/scripts/backup-postgresql.ps1" "$STAGE/scripts/"

ISCC="${ISCC:-$(command -v ISCC.exe || command -v iscc || true)}"
if [ -z "$ISCC" ]; then
    echo "Staging folder ready: $STAGE"
    echo "Inno Setup not found: set ISCC to compile the installer"
    exit 0
fi

ARGS=("/DAppVersion=$VERSION" "/DStage=$(cygpath -w "$STAGE")")
[ "$MODE" = patch ] && ARGS+=("/DPatch")
(cd "$HERE" && "$ISCC" "${ARGS[@]}" aguarapos.iss)
ls -la "$HERE"/output
