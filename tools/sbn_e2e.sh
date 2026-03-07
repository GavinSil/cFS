#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
BUILD_ROOT=${O:-build}
CPU_ROOT="$ROOT_DIR/$BUILD_ROOT/exe"
EXAMPLE_DIR="$ROOT_DIR/apps/sbn/examples/cFS"
LOG_DIR="$ROOT_DIR/$BUILD_ROOT/sbn-e2e"
EXPECTED_PACKETS=${EXPECTED_PACKETS:-4}
STARTUP_WAIT=${STARTUP_WAIT:-5}
PACKET_WAIT=${PACKET_WAIT:-10}
TO_PORT=${TO_PORT:-2235}
SBN_PROTOCOL=${SBN_PROTOCOL:-UDP}
SBN_PROTOCOL=${SBN_PROTOCOL:-UDP}

CPU1_BIN="$CPU_ROOT/cpu1/core-cpu1"
CPU2_BIN="$CPU_ROOT/cpu2/core-cpu2"
CPU1_DIR="$CPU_ROOT/cpu1"
CPU2_DIR="$CPU_ROOT/cpu2"
CPU1_LOG="$LOG_DIR/cpu1.log"
CPU2_LOG="$LOG_DIR/cpu2.log"
RECV_LOG="$LOG_DIR/to_recv.log"

cleanup() {
    if [[ -n "${RECV_PID:-}" ]]; then
        kill "$RECV_PID" >/dev/null 2>&1 || true
    fi

    if [[ -n "${CPU2_PID:-}" ]]; then
        kill "$CPU2_PID" >/dev/null 2>&1 || true
    fi

    if [[ -n "${CPU1_PID:-}" ]]; then
        kill "$CPU1_PID" >/dev/null 2>&1 || true
    fi

    wait >/dev/null 2>&1 || true
}

require_file() {
    local path=$1

    if [[ ! -e "$path" ]]; then
        echo "missing required path: $path" >&2
        exit 1
    fi
}

wait_for_log() {
    local pattern=$1
    local file=$2
    local timeout=$3
    local start

    start=$(date +%s)
    while true; do
        if [[ -f "$file" ]] && grep -q "$pattern" "$file"; then
            return 0
        fi

        if (( $(date +%s) - start >= timeout )); then
            return 1
        fi

        sleep 1
    done
}

count_packets() {
    local file=$1

    if [[ ! -f "$file" ]]; then
        echo 0
        return
    fi

    grep -c "<CCSDSPri MID=0x9f3" "$file" || true
}

mkdir -p "$LOG_DIR"
: > "$CPU1_LOG"
: > "$CPU2_LOG"
: > "$RECV_LOG"

require_file "$CPU1_BIN"
require_file "$CPU2_BIN"
require_file "$EXAMPLE_DIR/cisend"
require_file "$EXAMPLE_DIR/to_recv"

trap cleanup EXIT

pkill -f 'core-cpu1|core-cpu2|to_recv' >/dev/null 2>&1 || true

(
    cd "$CPU1_DIR"
    exec "$CPU1_BIN" -R PO
) > "$CPU1_LOG" 2>&1 &
CPU1_PID=$!

(
    cd "$CPU2_DIR"
    exec "$CPU2_BIN" -R PO
) > "$CPU2_LOG" 2>&1 &
CPU2_PID=$!

sleep "$STARTUP_WAIT"

if ! wait_for_log "CI_LAB listening on UDP port: 1234" "$CPU1_LOG" "$PACKET_WAIT"; then
    echo "CPU1 CI_LAB did not reach command-ready state in $CPU1_LOG" >&2
    tail -n 40 "$CPU1_LOG" >&2 || true
    exit 1
fi

if ! wait_for_log "TO Lab Initialized" "$CPU2_LOG" "$PACKET_WAIT"; then
    echo "CPU2 TO_LAB did not initialize in $CPU2_LOG" >&2
    tail -n 40 "$CPU2_LOG" >&2 || true
    exit 1
fi

(
    cd "$EXAMPLE_DIR"
    exec ./to_recv "$TO_PORT"
) > "$RECV_LOG" 2>&1 &
RECV_PID=$!

sleep 1

(
    cd "$EXAMPLE_DIR"
    printf '\xF3\x09\x00\x00\x00\x00\x10' | ./cisend --mid=0x1880 --cc=2
    sleep 1
    printf '127.0.0.1       ' | ./cisend --mid=0x1880 --cc=6
    sleep 1
    for ((i = 0; i < EXPECTED_PACKETS; ++i)); do
        printf '' | ./cisend --mid=0x19F2 --cc=0
        sleep 1
    done
)

if ! wait_for_log "<CCSDSPri MID=0x9f3" "$RECV_LOG" "$PACKET_WAIT"; then
    echo "No remapped telemetry observed in $RECV_LOG" >&2
    tail -n 40 "$CPU1_LOG" >&2 || true
    tail -n 40 "$CPU2_LOG" >&2 || true
    tail -n 40 "$RECV_LOG" >&2 || true
    exit 1
fi

RECV_COUNT=$(count_packets "$RECV_LOG")
if (( RECV_COUNT < EXPECTED_PACKETS )); then
    echo "Expected at least $EXPECTED_PACKETS remapped packets but found $RECV_COUNT" >&2
    tail -n 40 "$RECV_LOG" >&2 || true
    exit 1
fi

echo "SBN ${SBN_PROTOCOL} e2e PASS: observed $RECV_COUNT remapped packets on UDP port $TO_PORT"
echo "CPU1 log: $CPU1_LOG"
echo "CPU2 log: $CPU2_LOG"
echo "Receiver log: $RECV_LOG"