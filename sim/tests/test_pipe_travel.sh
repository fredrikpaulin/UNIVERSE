#!/bin/bash
# test_pipe_travel.sh — Integration tests for Phase 2: Interstellar Navigation
set -e

BIN="./build/universe"

echo "=== Pipe Travel Integration Tests ==="
echo ""

# Send a sequence: tick, scan, travel, tick, tick
CMDS=$(cat <<'EOF'
{"cmd":"tick","actions":{}}
{"cmd":"scan","probe_id":"1-1"}
{"cmd":"tick","actions":{"1-1":{"action":"travel_to_system","target_system_id":"6083314598184956166-3516480288839535305"}}}
{"cmd":"tick","actions":{}}
{"cmd":"tick","actions":{}}
EOF
)

OUTPUT=$(echo "$CMDS" | LD_LIBRARY_PATH=. $BIN --pipe --seed 42 2>/dev/null)

# Parse each line: init, tick1, scan, tick2 (travel initiated), tick3, tick4
echo "$OUTPUT" | python3 -c '
import sys, json

lines = [json.loads(l) for l in sys.stdin.read().strip().split("\n")]
passed = 0
failed = 0

def check(cond, label):
    global passed, failed
    if cond:
        passed += 1
    else:
        print(f"  FAIL: {label}", file=sys.stderr)
        failed += 1

# Line 0: init ready
check(lines[0].get("ready") == True, "init ready")

# Line 1: first tick
check(lines[1].get("ok") == True, "tick1 ok")
obs1 = lines[1]["observations"][0]
check(obs1["status"] == "active", "tick1 status is active")
check(obs1["location"] == "in_system", "tick1 location is in_system")

# Line 2: scan result
print("Test: Scan command", file=sys.stderr)
scan = lines[2]
check(scan.get("ok") == True, "scan ok")
check(scan.get("probe_id") == "1-1", "scan probe_id")
check(len(scan.get("systems", [])) > 0, "scan found systems")

sys0 = scan["systems"][0]
check(isinstance(sys0.get("system_id"), str), "scan system_id is string")
check(isinstance(sys0.get("name"), str) and len(sys0["name"]) > 0, "scan system has name")
check(sys0.get("star_class", -1) >= 0, "scan star_class >= 0")
check(sys0.get("star_count", 0) > 0, "scan star_count > 0")
check(sys0.get("distance_ly", 0) > 0, "scan distance_ly > 0")
check(sys0.get("estimated_travel_ticks", 0) > 0, "scan estimated_travel_ticks > 0")
check(len(sys0.get("position", [])) == 3, "scan position is [x,y,z]")
check(len(sys0.get("sector", [])) == 3, "scan sector is [x,y,z]")

# Verify sorted by distance
for i in range(1, len(scan["systems"])):
    check(scan["systems"][i]["distance_ly"] >= scan["systems"][i-1]["distance_ly"],
          "scan sorted by distance")

# Line 3: tick after travel_to_system — probe should be traveling
print("Test: Travel initiation", file=sys.stderr)
check(lines[3].get("ok") == True, "tick2 ok")
obs2 = lines[3]["observations"][0]
check(obs2["status"] == "traveling", "tick2 status is traveling")
check(obs2["location"] == "interstellar", "tick2 location is interstellar")
check(obs2["position"]["travel_remaining_ly"] > 0, "tick2 travel_remaining > 0")
check(obs2.get("system") is None, "tick2 system is null when interstellar")

# Line 4: tick3 — still traveling
print("Test: Travel progress", file=sys.stderr)
obs3 = lines[4]["observations"][0]
check(obs3["status"] == "traveling", "tick3 still traveling")
rem2 = obs2["position"]["travel_remaining_ly"]
rem3 = obs3["position"]["travel_remaining_ly"]
check(rem3 <= rem2, "tick3 travel_remaining <= initial")

# Line 5: tick4 — still traveling, after several ticks should have moved
obs4 = lines[5]["observations"][0]
rem4 = obs4["position"]["travel_remaining_ly"]
check(rem4 <= rem3, "tick4 travel_remaining <= tick3")

# Over multiple ticks, remaining should decrease (at 0.15c/365 ticks ~0.0004 ly/tick)
# rem2 - rem4 should be >= 2 * 0.0004 = 0.0008 after 2+ ticks of travel
check(rem2 - rem4 > 0.0005, "travel_remaining decreased measurably over ticks")

# Fuel should decrease slightly (0.5 kg/ly * ~0.0004 ly/tick = ~0.0002 kg/tick)
# With float rounding, check >= 0
check(obs4["fuel"] <= obs1["fuel"], "fuel not increasing during travel")

print(f"\n=== Results: {passed} passed, {failed} failed ===", file=sys.stderr)
sys.exit(1 if failed > 0 else 0)
' 2>&1
