#!/usr/bin/env bash
# Build the installer, or a patch, from a release build.
#
#   installer/build.sh BUILD_DIR PG_CLIENT_BIN [patch]
#
#   BUILD_DIR       a release build (its bin/ has aguara.exe)
#   PG_CLIENT_BIN   pgsql/bin of the 32-bit PostgreSQL 10 zip, unpacked whole: the
#                   full installer also carries the server, from next to it
#
# Stops if autoVersion.py says this can't be released as it is.
# Needs Python 3, and Qt and MinGW on PATH. Without ISCC (Inno Setup 6) it leaves the staging
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
. "$ROOT/scripts/find-python.sh"
[ -n "$PYTHON" ] || { echo "Python 3 not found"; exit 1; }
$PYTHON -B "$ROOT/autoVersion.py" check --release

number() { sed -n "s/^#define VERSION_$1 //p" "$ROOT/src/interfaces/version.h" | tr -d '\r'; }
NUMERIC="$(number MAJOR).$(number MINOR).$(number PATCH)"
VERSION="$NUMERIC$(sed -n 's/^#define VERSION_SUFFIX "\(.*\)".*/\1/p' "$ROOT/src/interfaces/version.h")"
echo "== AguaraPOS $VERSION ($MODE)"

rm -rf "$STAGE"
mkdir -p "$STAGE/modules" "$STAGE/doc" "$STAGE/scripts"
cp "$BIN/aguara.exe" "$BIN/aguara_interfaces.dll" "$STAGE/"
cp "$BIN"/modules/*.dll "$STAGE/modules/"

# The symbols go to a file for the telemetry server, and nothing that reaches a
# till carries them. The full installer and the patch of one build get the same file.
mkdir -p "$HERE/output"
$PYTHON -B "$ROOT/scripts/export-symbols.py" --version "$VERSION" \
    --output "$(cygpath -w "$HERE/output/aguarapos-simbolos-$VERSION.json.gz")" \
    "$STAGE/aguara.exe" "$STAGE/aguara_interfaces.dll" "$STAGE"/modules/*.dll

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

# The server, for the caja that hosts the data: its own folder, so it's an
# option in the installer and patches leave it alone. Only what it needs to run.
PG_STAGE="$BUILD_DIR/stage-pg"
if [ "$MODE" != patch ]; then
    PG_DIR="$(dirname "$PG_CLIENT_BIN")"
    [ -f "$PG_DIR/share/postgresql.conf.sample" ] || {
        echo "No PostgreSQL server in $PG_DIR: unpack bin, lib and share of the zip there"; exit 1; }
    rm -rf "$PG_STAGE"
    mkdir -p "$PG_STAGE"
    cp -r "$PG_DIR/bin" "$PG_DIR/lib" "$PG_DIR/share" "$PG_STAGE/"
    rm -f "$PG_STAGE"/bin/stackbuilder.exe "$PG_STAGE"/bin/wx*.dll "$PG_STAGE"/bin/icutest*.dll \
          "$PG_STAGE"/bin/icutu*.dll "$PG_STAGE"/bin/*regress*.exe "$PG_STAGE"/bin/isolationtester.exe \
          "$PG_STAGE"/bin/ecpg.exe "$PG_STAGE"/lib/*.lib "$PG_STAGE"/lib/*.a
    rm -rf "$PG_STAGE/lib/pgxs"
    cp "$STAGE/msvcr120.dll" "$STAGE/msvcp120.dll" "$PG_STAGE/bin/"
    cp "$HERE/LICENSE-PostgreSQL.txt" "$PG_STAGE/"
    cp "$HERE/pg-setup.cmd" "$STAGE/scripts/"
fi

cp "$ROOT"/doc/manual/*.pdf "$STAGE/doc/"
cp "$ROOT/doc/deploy/varios-puestos-postgresql.md" "$STAGE/doc/"
cp "$ROOT/scripts/backup-postgresql.ps1" "$STAGE/scripts/"

ISCC="${ISCC:-$(command -v ISCC.exe || command -v iscc || true)}"
if [ -z "$ISCC" ]; then
    echo "Staging folder ready: $STAGE"
    echo "Inno Setup not found: set ISCC to compile the installer"
    exit 0
fi

ARGS=("/DAppVersion=$VERSION" "/DNumericVersion=$NUMERIC" "/DStage=$(cygpath -w "$STAGE")")
if [ "$MODE" = patch ]; then ARGS+=("/DPatch"); else ARGS+=("/DPgStage=$(cygpath -w "$PG_STAGE")"); fi
# Git Bash would turn /DName=value into a path
(cd "$HERE" && MSYS2_ARG_CONV_EXCL="*" "$ISCC" "${ARGS[@]}" aguarapos.iss)
ls -la "$HERE"/output
