#!/bin/bash
# test_pipe_diplomacy.sh — Integration tests for Phase 7-8: Diplomacy, Research
set -e
BIN="./build/universe"

echo "=== Pipe Diplomacy & Research Integration Tests ==="
echo ""

# Test 1: New observation fields present
echo "Test: Diplomacy/research obs fields present"
RESP=$(echo '{"cmd":"tick","actions":{}}' | LD_LIBRARY_PATH=. $BIN --pipe --seed 42 2>/dev/null | tail -1)
echo "$RESP" | python3 -c '
import sys, json
d = json.load(sys.stdin)
o = d["observations"][0]
p, f = 0, 0
def chk(c, l):
    global p, f
    if c: p += 1
    else: f += 1; print(f"  FAIL: {l}", file=sys.stderr)
chk(isinstance(o.get("claims"), list), "claims is array")
chk(isinstance(o.get("proposals"), list), "proposals is array")
chk(isinstance(o.get("trust"), list), "trust is array")
chk(len(o["claims"]) == 0, "no claims initially")
chk(len(o["proposals"]) == 0, "no proposals initially")
print(f"  {p} passed, {f} failed", file=sys.stderr)
sys.exit(1 if f > 0 else 0)
' 2>&1
echo ""

# Test 2: claim_system creates a claim
echo "Test: Claim system action"
RESP=$(printf '{"cmd":"tick","actions":{"1-1":{"action":"claim_system"}}}\n{"cmd":"tick","actions":{}}\n' | LD_LIBRARY_PATH=. $BIN --pipe --seed 42 2>/dev/null | tail -1)
echo "$RESP" | python3 -c '
import sys, json
d = json.load(sys.stdin)
o = d["observations"][0]
p, f = 0, 0
def chk(c, l):
    global p, f
    if c: p += 1
    else: f += 1; print(f"  FAIL: {l}", file=sys.stderr)
claims = o.get("claims", [])
chk(len(claims) == 1, "one claim exists")
if claims:
    chk(claims[0]["claimer"] == "1-1", "claimer is probe 1-1")
    chk(isinstance(claims[0]["tick"], int), "tick is int")
print(f"  {p} passed, {f} failed", file=sys.stderr)
sys.exit(1 if f > 0 else 0)
' 2>&1
echo ""

# Test 3: revoke_claim removes the claim
echo "Test: Revoke claim action"
RESP=$(printf '{"cmd":"tick","actions":{"1-1":{"action":"claim_system"}}}\n{"cmd":"tick","actions":{"1-1":{"action":"revoke_claim"}}}\n{"cmd":"tick","actions":{}}\n' | LD_LIBRARY_PATH=. $BIN --pipe --seed 42 2>/dev/null | tail -1)
echo "$RESP" | python3 -c '
import sys, json
d = json.load(sys.stdin)
o = d["observations"][0]
p, f = 0, 0
def chk(c, l):
    global p, f
    if c: p += 1
    else: f += 1; print(f"  FAIL: {l}", file=sys.stderr)
claims = o.get("claims", [])
chk(len(claims) == 0, "claim removed after revoke")
print(f"  {p} passed, {f} failed", file=sys.stderr)
sys.exit(1 if f > 0 else 0)
' 2>&1
echo ""

# Test 4: propose creates an active proposal
echo "Test: Propose action"
RESP=$(printf '{"cmd":"tick","actions":{"1-1":{"action":"propose","text":"Explore north"}}}\n{"cmd":"tick","actions":{}}\n' | LD_LIBRARY_PATH=. $BIN --pipe --seed 42 2>/dev/null | tail -1)
echo "$RESP" | python3 -c '
import sys, json
d = json.load(sys.stdin)
o = d["observations"][0]
p, f = 0, 0
def chk(c, l):
    global p, f
    if c: p += 1
    else: f += 1; print(f"  FAIL: {l}", file=sys.stderr)
props = o.get("proposals", [])
chk(len(props) == 1, "one proposal exists")
if props:
    chk(props[0]["proposer"] == "1-1", "proposer is 1-1")
    chk(props[0]["text"] == "Explore north", "text matches")
    chk(props[0]["for"] == 0, "no votes yet")
    chk(props[0]["against"] == 0, "no votes against yet")
    chk(isinstance(props[0]["deadline"], int), "deadline is int")
print(f"  {p} passed, {f} failed", file=sys.stderr)
sys.exit(1 if f > 0 else 0)
' 2>&1
echo ""

# Test 5: vote on a proposal
echo "Test: Vote action"
RESP=$(printf '{"cmd":"tick","actions":{"1-1":{"action":"propose","text":"Go west"}}}\n{"cmd":"tick","actions":{"1-1":{"action":"vote","proposal":0,"favor":true}}}\n{"cmd":"tick","actions":{}}\n' | LD_LIBRARY_PATH=. $BIN --pipe --seed 42 2>/dev/null | tail -1)
echo "$RESP" | python3 -c '
import sys, json
d = json.load(sys.stdin)
o = d["observations"][0]
p, f = 0, 0
def chk(c, l):
    global p, f
    if c: p += 1
    else: f += 1; print(f"  FAIL: {l}", file=sys.stderr)
props = o.get("proposals", [])
chk(len(props) == 1, "proposal exists")
if props:
    chk(props[0]["for"] == 1, "one vote for")
print(f"  {p} passed, {f} failed", file=sys.stderr)
sys.exit(1 if f > 0 else 0)
' 2>&1
echo ""

# Test 6: research starts and shows progress
echo "Test: Research action progress"
RESP=$(printf '{"cmd":"tick","actions":{"1-1":{"action":"research","domain":1}}}\n{"cmd":"tick","actions":{"1-1":{"action":"research","domain":1}}}\n{"cmd":"tick","actions":{}}\n' | LD_LIBRARY_PATH=. $BIN --pipe --seed 42 2>/dev/null | tail -1)
echo "$RESP" | python3 -c '
import sys, json
d = json.load(sys.stdin)
o = d["observations"][0]
p, f = 0, 0
def chk(c, l):
    global p, f
    if c: p += 1
    else: f += 1; print(f"  FAIL: {l}", file=sys.stderr)
res = o.get("research")
chk(res is not None, "research field present")
if res:
    chk(res["domain"] == 1, "domain is sensors (1)")
    chk(res["progress"] > 0, "progress > 0")
    chk(res["ticks_remaining"] > 0, "ticks remaining > 0")
print(f"  {p} passed, {f} failed", file=sys.stderr)
sys.exit(1 if f > 0 else 0)
' 2>&1
echo ""

# Test 7: research not shown when not active
echo "Test: Research absent when not researching"
RESP=$(echo '{"cmd":"tick","actions":{}}' | LD_LIBRARY_PATH=. $BIN --pipe --seed 42 2>/dev/null | tail -1)
echo "$RESP" | python3 -c '
import sys, json
d = json.load(sys.stdin)
o = d["observations"][0]
p, f = 0, 0
def chk(c, l):
    global p, f
    if c: p += 1
    else: f += 1; print(f"  FAIL: {l}", file=sys.stderr)
chk("research" not in o, "no research field when idle")
print(f"  {p} passed, {f} failed", file=sys.stderr)
sys.exit(1 if f > 0 else 0)
' 2>&1
echo ""

# Test 8: all new actions parse without error
echo "Test: New actions parse OK"
for act in claim_system revoke_claim propose vote research share_tech; do
    RESP=$(printf '{"cmd":"tick","actions":{"1-1":{"action":"%s"}}}\n' "$act" | LD_LIBRARY_PATH=. $BIN --pipe --seed 42 2>/dev/null | tail -1)
    OK=$(echo "$RESP" | python3 -c 'import sys,json; d=json.load(sys.stdin); print(d.get("ok",""))')
    if [ "$OK" = "True" ]; then
        echo "  $act: OK"
    else
        echo "  FAIL: $act did not return ok"
        exit 1
    fi
done
echo ""

echo "=== All Diplomacy & Research Tests Complete ==="
