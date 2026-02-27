#!/bin/bash
# test_pipe_obs.sh — Integration tests for enriched pipe observations
# Validates Phase 1: Richer Observations JSON structure
set -e

BIN="./build/universe"

echo "=== Pipe Observation Integration Tests ==="
echo ""

# Get a tick response
TICK_RESP=$(echo '{"cmd":"tick","actions":{}}' | LD_LIBRARY_PATH=. $BIN --pipe --seed 42 2>/dev/null | tail -1)

# Run all checks in a single Python script
RESULT=$(echo "$TICK_RESP" | python3 -c '
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

# Valid JSON + basic structure
check(d.get("ok") == True, "ok is true")
check(d.get("tick") == 1, "tick is 1")
check(len(d["observations"]) == 1, "one observation")

# Core fields
print("Test: Core fields present", file=sys.stderr)
check(o.get("probe_id") == "1-1", "probe_id")
check(o.get("name") == "Bob", "name is Bob")
check(o.get("status") == "active", "status")
check(isinstance(o.get("hull"), float), "hull")
check(isinstance(o.get("energy"), float), "energy")
check(isinstance(o.get("fuel"), float), "fuel")
check(o.get("generation") == 0, "generation")
check(len(o.get("tech", [])) == 10, "tech array length")
check(o.get("location") == "in_system", "location")

# Resources
print("Test: Resources object", file=sys.stderr)
r = o.get("resources", {})
for key in ["iron","silicon","rare_earth","water","hydrogen","helium3","carbon","uranium","exotic"]:
    check(key in r and isinstance(r[key], (int, float)), f"resources.{key}")

# Position
print("Test: Position object", file=sys.stderr)
p = o.get("position", {})
check(len(p.get("sector", [])) == 3, "position.sector")
check(isinstance(p.get("system_id"), str) and "-" in p["system_id"], "position.system_id")
check(isinstance(p.get("body_id"), str), "position.body_id")
check(len(p.get("heading", [])) == 3, "position.heading")
check(len(p.get("destination", [])) == 3, "position.destination")
check(isinstance(p.get("travel_remaining_ly"), (int, float)), "position.travel_remaining_ly")

# Capabilities
print("Test: Capabilities object", file=sys.stderr)
c = o.get("capabilities", {})
check(c.get("max_speed_c", 0) > 0, "capabilities.max_speed_c")
check(c.get("sensor_range_ly", 0) > 0, "capabilities.sensor_range_ly")
check(c.get("mining_rate", 0) > 0, "capabilities.mining_rate")
check(c.get("construction_rate", 0) > 0, "capabilities.construction_rate")
check(c.get("compute_capacity", 0) > 0, "capabilities.compute_capacity")

# Recent events (empty on tick 1)
print("Test: Recent events array", file=sys.stderr)
check(isinstance(o.get("recent_events"), list), "recent_events is array")

# System details
print("Test: System details", file=sys.stderr)
s = o.get("system", {})
check(isinstance(s.get("name"), str) and len(s["name"]) > 0, "system.name")
check(s.get("star_count", 0) > 0, "system.star_count")
check(s.get("planet_count", 0) > 0, "system.planet_count")

# Stars
print("Test: Star details", file=sys.stderr)
st = s.get("stars", [{}])[0]
check(isinstance(st.get("name"), str) and len(st["name"]) > 0, "star.name")
check(isinstance(st.get("class"), int), "star.class")
check(st.get("mass_solar", 0) > 0, "star.mass_solar")
check(st.get("temp_k", 0) > 0, "star.temp_k")
check(st.get("luminosity_solar", 0) > 0, "star.luminosity_solar")
check(isinstance(st.get("metallicity"), (int, float)), "star.metallicity")

# Planets — enhanced
print("Test: Enhanced planet details", file=sys.stderr)
pl = s.get("planets", [{}])[0]
check(isinstance(pl.get("name"), str) and len(pl["name"]) > 0, "planet.name")
check(isinstance(pl.get("type"), int), "planet.type")
check(pl.get("mass_earth", 0) > 0, "planet.mass_earth")
check(pl.get("radius_earth", 0) > 0, "planet.radius_earth")
check(pl.get("orbital_radius_au", 0) > 0, "planet.orbital_radius_au")
check(pl.get("orbital_period_days", 0) > 0, "planet.orbital_period_days")
check(pl.get("surface_temp_k", 0) > 0, "planet.surface_temp_k")
check(isinstance(pl.get("atmosphere_pressure_atm"), (int, float)), "planet.atmosphere_pressure_atm")
check(isinstance(pl.get("water_coverage"), (int, float)), "planet.water_coverage")
check(isinstance(pl.get("habitability"), (int, float)), "planet.habitability")
check(isinstance(pl.get("magnetic_field"), (int, float)), "planet.magnetic_field")
check(isinstance(pl.get("rings"), bool), "planet.rings")
check(isinstance(pl.get("moon_count"), int), "planet.moon_count")
sc = pl.get("survey_complete", [])
check(len(sc) == 5 and all(isinstance(x, bool) for x in sc), "planet.survey_complete")

# Planet resources
print("Test: Planet resource abundances", file=sys.stderr)
pr = pl.get("resources", {})
check("iron" in pr and "exotic" in pr, "planet.resources keys")
check(all(0 <= v <= 1 for v in pr.values()), "planet.resources in [0,1]")

# Nearby probes (empty with single probe)
print("Test: Nearby probes array", file=sys.stderr)
check(isinstance(o.get("nearby_probes"), list), "nearby_probes is array")
check(len(o.get("nearby_probes", [1])) == 0, "nearby_probes empty (single probe)")

print(f"\n=== Results: {passed} passed, {failed} failed ===", file=sys.stderr)
sys.exit(1 if failed > 0 else 0)
' 2>&1)

echo "$RESULT"
