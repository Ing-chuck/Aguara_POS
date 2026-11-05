#!/usr/bin/env bash
# Run every test suite of a build, writing each result to a file, and print
# a summary (plus the details of anything that failed).
#
#   tests/run-tests.sh <build dir> [results dir]
#
# Results: <results dir>/<suite>.txt (readable) and <suite>.xml (JUnit, for
# CI). Default results dir: <build dir>/test-results.
# Exit status: 0 when every suite passed (skips are fine).
#
# "make check" in the build folder runs the same suites; this script exists
# because a test run without a console window (some CI runners, IDE shells)
# can lose Qt Test's console output, while files always work.
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
to_unix() { if command -v cygpath > /dev/null; then cygpath -u "$1"; else echo "$1"; fi; }

BUILD="$(to_unix "${1:?usage: run-tests.sh <build dir> [results dir]}")"
OUT="$(to_unix "${2:-$BUILD/test-results}")"
QT_BIN="${QT_BIN:-$(dirname "$(command -v qmake)")}"
mkdir -p "$OUT"
# absolute paths: the suites run from tests/bin, so relative ones would point nowhere
BUILD="$(cd "$BUILD" && pwd)" || { echo "no build folder $1"; exit 1; }
OUT="$(cd "$OUT" && pwd)"
# the suites need aguara_interfaces.dll (and libpq.dll) from bin/, and Qt's DLLs
export PATH="$BUILD/bin:$(to_unix "$QT_BIN"):$PATH"

cd "$BUILD/tests/bin" || { echo "no test executables in $BUILD/tests/bin"; exit 1; }
status=0
for exe in tst_*; do
    case "$exe" in
        *.exe) name="${exe%.exe}" ;;
        *.*)   continue ;;                   # .pdb, .ilk, ...
        *)     name="$exe" ;;                # other systems: no extension
    esac
    [ -f "$exe" ] && [ -x "$exe" ] || continue
    rm -f "$OUT/$name.txt" "$OUT/$name.xml"     # a suite that cannot start must not show the last result
    ./"$exe" -o "$OUT/$name.txt,txt" -o "$OUT/$name.xml,junitxml" < /dev/null > "$OUT/$name.stdout" 2>&1
    code=$?
    totals="$(grep '^Totals' "$OUT/$name.txt" 2>/dev/null)"
    printf '%-20s %s\n' "$name" "${totals:-did not finish (exit $code)}"
    if [ $code -ne 0 ]; then
        status=1
        grep -E -A2 '^(FAIL!|XPASS)' "$OUT/$name.txt" 2>/dev/null | sed 's/^/    /'
        [ -z "$totals" ] && sed 's/^/    /' "$OUT/$name.stdout"
    fi
done
# versions: the public headers match the API level, and the tool itself
if command -v python > /dev/null; then
    python -B "$ROOT/autoVersion.py" check || status=1
    python -B "$ROOT/tests/test_autoversion.py" 2>&1 | tail -3 | sed 's/^/    /'
    [ "${PIPESTATUS[0]}" -eq 0 ] || status=1
else
    echo "python not found: autoVersion.py not checked"
fi
[ $status -eq 0 ] && echo "All suites passed." || echo "FAILED: see $OUT"
exit $status
