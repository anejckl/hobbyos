#!/usr/bin/env bash
#
# qemu_smoke.sh — Boot HobbyOS in QEMU and verify serial output.
#
# Usage: bash tests/qemu_smoke.sh
# Expects hobbyos.iso to already exist (run 'make iso' first).
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
ISO="$ROOT_DIR/hobbyos.iso"
SERIAL_LOG="$SCRIPT_DIR/serial_output.log"
EXPECTED="$SCRIPT_DIR/qemu_smoke.exp"
TIMEOUT=45

# ---- Preflight checks ----

if [ ! -f "$ISO" ]; then
    echo "ERROR: $ISO not found. Run 'make iso' first."
    exit 1
fi

if [ ! -f "$EXPECTED" ]; then
    echo "ERROR: $EXPECTED not found."
    exit 1
fi

if ! command -v qemu-system-x86_64 &>/dev/null; then
    echo "ERROR: qemu-system-x86_64 not found in PATH."
    exit 1
fi

# ---- Boot QEMU ----

rm -f "$SERIAL_LOG"

echo "Booting HobbyOS in QEMU (timeout: ${TIMEOUT}s)..."

# Run QEMU in background with serial output to file
qemu-system-x86_64 \
    -cdrom "$ISO" \
    -serial file:"$SERIAL_LOG" \
    -display none \
    -m 128M \
    -no-reboot \
    -no-shutdown \
    -device isa-debug-exit,iobase=0xf4,iosize=0x04 \
    -netdev user,id=net0 -device e1000,netdev=net0 \
    &
QEMU_PID=$!

# Wait for timeout then kill QEMU
sleep "$TIMEOUT"
if kill -0 "$QEMU_PID" 2>/dev/null; then
    kill "$QEMU_PID" 2>/dev/null || true
    wait "$QEMU_PID" 2>/dev/null || true
fi

# ---- Check serial output ----

if [ ! -f "$SERIAL_LOG" ]; then
    echo "ERROR: Serial log not created — QEMU may have failed to start."
    exit 1
fi

echo ""
echo "Checking serial output for expected boot messages..."
echo ""

PASS=0
FAIL=0
TOTAL=0

while IFS= read -r pattern; do
    # Skip empty lines and comments
    [[ -z "$pattern" || "$pattern" == \#* ]] && continue

    TOTAL=$((TOTAL + 1))
    if grep -qF "$pattern" "$SERIAL_LOG"; then
        echo "  PASS: $pattern"
        PASS=$((PASS + 1))
    else
        echo "  FAIL: $pattern"
        FAIL=$((FAIL + 1))
    fi
done < "$EXPECTED"

echo ""
echo "Results: $PASS passed, $FAIL failed, $TOTAL total"

if [ "$FAIL" -gt 0 ]; then
    echo "QEMU SMOKE TEST FAILED"
    echo ""
    echo "Serial output was:"
    cat "$SERIAL_LOG"
    exit 1
fi

echo "QEMU SMOKE TEST PASSED"
rm -f "$SERIAL_LOG"
exit 0
