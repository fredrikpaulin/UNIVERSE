#!/bin/bash
# test_pipe_coord.sh — Integration tests for Phase 6: Multi-Agent Coordination
# Tests: place_beacon, build_structure, send_message, trade observation fields
set -e

BIN="./build/universe"

echo "=== Pipe Coordination Integration Tests ==="
echo ""

# Test 1: Observation includes coordination arrays
echo "Test: Coordination arrays present in observations"
RESP=$(echo '{"cmd":"tick","actions":{}}' | LD_LIBRARY_PATH=. $BIN --pipe --seed 42 2>/dev/null | tail -1)
echo "$RESP" | python3 -c '
import sys, json
d = json.load(sys.stdin)
o = d["observations"][0]
passed = 0
failed = 0

def check(cond, label):
    global passed, failed
    if cond:
        passed += 1
    else:
        print(f"  FAIL: {label}", file=sys.stderr)
        failed += 1

check(isinstance(o.get("inbox"), list), "inbox is array")
check(isinstance(o.get("visible_beacons"), list), "visible_beacons is array")
check(isinstance(o.get("visible_structures"), list), "visible_structures is array")
check(isinstance(o.get("pending_trades"), list), "pending_trades is array")
check(len(o["inbox"]) == 0, "inbox empty initially")
check(len(o["visible_beacons"]) == 0, "no beacons initially")
check(len(o["visible_structures"]) == 0, "no structures initially")
check(len(o["pending_trades"]) == 0, "no trades initially")

print(f"  {passed} passed, {failed} failed", file=sys.stderr)
sys.exit(1 if failed > 0 else 0)
' 2>&1
echo ""

# Test 2: place_beacon creates a beacon visible in observations
echo "Test: Place beacon action"
RESP=$(printf '{"cmd":"tick","actions":{"1-1":{"action":"place_beacon","message":"test beacon"}}}\n{"cmd":"tick","actions":{}}\n' | LD_LIBRARY_PATH=. $BIN --pipe --seed 42 2>/dev/null | tail -1)
echo "$RESP" | python3 -c '
import sys, json
d = json.load(sys.stdin)
o = d["observations"][0]
passed = 0
failed = 0

def check(cond, label):
    global passed, failed
    if cond:
        passed += 1
    else:
        print(f"  FAIL: {label}", file=sys.stderr)
        failed += 1

beacons = o.get("visible_beacons", [])
check(len(beacons) == 1, "one beacon visible")
if beacons:
    check(beacons[0]["owner"] == "1-1", "beacon owner is probe 1-1")
    check(beacons[0]["message"] == "test beacon", "beacon message matches")
    check(isinstance(beacons[0]["placed_tick"], int), "placed_tick is int")

print(f"  {passed} passed, {failed} failed", file=sys.stderr)
sys.exit(1 if failed > 0 else 0)
' 2>&1
echo ""

# Test 3: build_structure starts a construction
echo "Test: Build structure action"
RESP=$(printf '{"cmd":"tick","actions":{"1-1":{"action":"build_structure","structure_type":0}}}\n{"cmd":"tick","actions":{}}\n' | LD_LIBRARY_PATH=. $BIN --pipe --seed 42 2>/dev/null | tail -1)
echo "$RESP" | python3 -c '
import sys, json
d = json.load(sys.stdin)
o = d["observations"][0]
passed = 0
failed = 0

def check(cond, label):
    global passed, failed
    if cond:
        passed += 1
    else:
        print(f"  FAIL: {label}", file=sys.stderr)
        failed += 1

structs = o.get("visible_structures", [])
check(len(structs) >= 1, "at least one structure visible")
if structs:
    s = structs[0]
    check(s["type"] == 0, "structure type is mining station (0)")
    check(s["complete"] == False, "structure not complete yet")
    check(s["progress"] > 0, "progress > 0 after 1 tick")
    check(s["progress"] < 1, "progress < 1 (not done)")
    check(isinstance(s["name"], str), "name is string")
    check(s["builder"] == "1-1", "builder is probe 1-1")

print(f"  {passed} passed, {failed} failed", file=sys.stderr)
sys.exit(1 if failed > 0 else 0)
' 2>&1
echo ""

# Test 4: Multiple beacons can be placed
echo "Test: Multiple beacons"
RESP=$(printf '{"cmd":"tick","actions":{"1-1":{"action":"place_beacon","message":"beacon A"}}}\n{"cmd":"tick","actions":{"1-1":{"action":"place_beacon","message":"beacon B"}}}\n{"cmd":"tick","actions":{}}\n' | LD_LIBRARY_PATH=. $BIN --pipe --seed 42 2>/dev/null | tail -1)
echo "$RESP" | python3 -c '
import sys, json
d = json.load(sys.stdin)
o = d["observations"][0]
passed = 0
failed = 0

def check(cond, label):
    global passed, failed
    if cond:
        passed += 1
    else:
        print(f"  FAIL: {label}", file=sys.stderr)
        failed += 1

beacons = o.get("visible_beacons", [])
check(len(beacons) == 2, "two beacons visible")

print(f"  {passed} passed, {failed} failed", file=sys.stderr)
sys.exit(1 if failed > 0 else 0)
' 2>&1
echo ""

# Test 5: Action names parse correctly for new actions
echo "Test: New action names parse"
RESP=$(printf '{"cmd":"tick","actions":{"1-1":{"action":"send_message","target":"1-1","content":"self msg"}}}\n' | LD_LIBRARY_PATH=. $BIN --pipe --seed 42 2>/dev/null | tail -1)
echo "$RESP" | python3 -c '
import sys, json
d = json.load(sys.stdin)
passed = 0
failed = 0

def check(cond, label):
    global passed, failed
    if cond:
        passed += 1
    else:
        print(f"  FAIL: {label}", file=sys.stderr)
        failed += 1

check(d.get("ok") == True, "tick ok after send_message")

print(f"  {passed} passed, {failed} failed", file=sys.stderr)
sys.exit(1 if failed > 0 else 0)
' 2>&1
echo ""

# Test 6: trade action doesn't crash (single probe, no valid target)
echo "Test: Trade action with self (no-op)"
RESP=$(printf '{"cmd":"tick","actions":{"1-1":{"action":"trade","target":"1-1","resource":"iron","amount":100}}}\n' | LD_LIBRARY_PATH=. $BIN --pipe --seed 42 2>/dev/null | tail -1)
echo "$RESP" | python3 -c '
import sys, json
d = json.load(sys.stdin)
passed = 0
failed = 0

def check(cond, label):
    global passed, failed
    if cond:
        passed += 1
    else:
        print(f"  FAIL: {label}", file=sys.stderr)
        failed += 1

check(d.get("ok") == True, "tick ok after trade action")

print(f"  {passed} passed, {failed} failed", file=sys.stderr)
sys.exit(1 if failed > 0 else 0)
' 2>&1
echo ""

echo "=== All Coordination Tests Complete ==="
