#!/usr/bin/env bash
# Run the test suites against a throwaway PostgreSQL server, including the
# PostgreSQL rows and tst_reconnect (which stops and restarts the server).
#
# Nothing touches a real server: a new data folder is created under
# $WORK_DIR, the server listens on localhost:$PG_PORT only, and it is
# stopped at the end (also on failure).
#
#   PG_BIN=/c/pgsql/bin  BUILD_DIR=build  tests/run-postgresql-tests.sh
#
# Variables:
#   PG_BIN         required: folder with initdb, pg_ctl and psql (any PostgreSQL >= 10;
#                  64-bit is fine, it's a separate process)
#   BUILD_DIR      the build folder of Aguara_POS.pro            (default: build)
#   PG_CLIENT_BIN  folder with the 32-bit PostgreSQL 10 client DLLs (libpq.dll, ...);
#                  copied into $BUILD_DIR/bin if given. Without it, the build
#                  must already have them (qmake AGUARA_LIBPQ_DIR=...).
#   PG_PORT        port for the test server                      (default: 54321)
#   WORK_DIR       where the data folder goes                    (default: $BUILD_DIR/pgtest)
#   TEST_COMMAND   what runs the tests      (default: tests/run-tests.sh $BUILD_DIR)
set -u

: "${PG_BIN:?set PG_BIN to the folder with initdb, pg_ctl and psql}"
BUILD_DIR="${BUILD_DIR:-build}"
PG_PORT="${PG_PORT:-54321}"
WORK_DIR="${WORK_DIR:-$BUILD_DIR/pgtest}"

# absolute paths: the tests get the data folder and pg_ctl, and run from tests/bin
mkdir -p "$WORK_DIR"
BUILD_DIR="$(cd "$BUILD_DIR" && pwd)" || { echo "no build folder $BUILD_DIR"; exit 1; }
WORK_DIR="$(cd "$WORK_DIR" && pwd)"
PG_BIN="$(cd "$PG_BIN" && pwd)" || { echo "no folder $PG_BIN"; exit 1; }
TEST_COMMAND="${TEST_COMMAND:-bash $(cd "$(dirname "$0")" && pwd)/run-tests.sh $BUILD_DIR}"

exe() { if [ -x "$PG_BIN/$1.exe" ]; then echo "$PG_BIN/$1.exe"; else echo "$PG_BIN/$1"; fi; }
INITDB="$(exe initdb)"; PG_CTL="$(exe pg_ctl)"; PSQL="$(exe psql)"
DATA="$WORK_DIR/data"

"$PSQL" --version
echo "== 1. new data folder $DATA"
rm -rf "$DATA"
mkdir -p "$WORK_DIR"
printf 'testsuper\n' > "$WORK_DIR/superpass.txt"
"$INITDB" -D "$DATA" -U postgres --pwfile="$WORK_DIR/superpass.txt" --auth=scram-sha-256 \
    -E UTF8 --locale=C > "$WORK_DIR/initdb.log" 2>&1 || { cat "$WORK_DIR/initdb.log"; exit 1; }

echo "== 2. start the server on localhost:$PG_PORT"
# the server's output goes to a file: an inherited pipe would keep this script waiting
"$PG_CTL" -D "$DATA" -o "-p $PG_PORT -c listen_addresses=localhost" -l "$WORK_DIR/pg.log" -w start \
    > "$WORK_DIR/pg_ctl.log" 2>&1 || { cat "$WORK_DIR/pg_ctl.log" "$WORK_DIR/pg.log"; exit 1; }
trap '"$PG_CTL" -D "$DATA" -m fast -w stop > /dev/null 2>&1' EXIT

echo "== 3. role aguara and database aguara_test (as in doc/deploy/varios-puestos-postgresql.md)"
PGPASSWORD=testsuper "$PSQL" -h localhost -p "$PG_PORT" -U postgres -v ON_ERROR_STOP=1 -q \
    -c "CREATE ROLE aguara LOGIN PASSWORD 'testpass'" \
    -c "CREATE DATABASE aguara_test OWNER aguara ENCODING 'UTF8' TEMPLATE template0" || exit 1

if [ -n "${PG_CLIENT_BIN:-}" ]; then
    echo "== 4. 32-bit client DLLs from $PG_CLIENT_BIN"
    for dll in libpq.dll libssl-1_1.dll libcrypto-1_1.dll libintl-8.dll libiconv-2.dll; do
        cp "$PG_CLIENT_BIN/$dll" "$BUILD_DIR/bin/" || exit 1
    done
fi

echo "== 5. tests"
export AGUARA_TEST_PG_HOST=localhost AGUARA_TEST_PG_PORT="$PG_PORT"
export AGUARA_TEST_PG_USER=aguara AGUARA_TEST_PG_PASSWORD=testpass
export AGUARA_TEST_PG_CTL="$PG_CTL" AGUARA_TEST_PG_DATA="$DATA"
$TEST_COMMAND < /dev/null
status=$?
echo "== tests finished with status $status (server log: $WORK_DIR/pg.log)"
exit $status
