# The Science Behind Project UNIVERSE

How does the simulation decide what a star system looks like? Where does a planet's temperature come from? Why does mining on a super-Earth feel sluggish? This document walks through the real (and slightly simplified) physics that drive the procedural universe.

## Stars

Every star system starts with a spectral classification. The simulation rolls a random number and picks a class from a distribution that mirrors the real Milky Way — about three quarters of all stars are dim red M-dwarfs, roughly one in eight is an orange K-dwarf, and the brilliant blue O-type giants are almost absurdly rare at three in a hundred thousand.

| Class | Colour | Temp (K) | Mass (M☉) | Share |
|-------|--------|----------|-----------|-------|
| M | Red | 2400–3700 | 0.08–0.45 | 76.5% |
| K | Orange | 3700–5200 | 0.45–0.80 | 12.1% |
| G | Yellow (like Sol) | 5200–6000 | 0.80–1.04 | 7.6% |
| F | Yellow-white | 6000–7500 | 1.04–1.40 | 3.0% |
| A | White | 7500–10000 | 1.40–2.10 | 0.6% |
| B | Blue-white | 10000–30000 | 2.10–16.0 | 0.13% |
| O | Blue | 30000–50000 | 16.0–90.0 | 0.003% |

The table also includes white dwarfs, neutron stars, and black holes as stellar remnants. Each star gets a mass, luminosity, and surface temperature randomly sampled within its class's range, plus an age (0.1–13 billion years) and a metallicity — a measure of how enriched the star is in heavy elements. High metallicity means more raw materials for planet formation, so metal-rich stars tend to generate more planets.

About 70% of systems have a single star. 25% are binaries, and 5% are triples. Companion stars sit very close together and mainly affect how many stable orbits the system can support — more stars means fewer planets.

## The Habitable Zone

A star's habitable zone — the orbital band where liquid water could exist on a planet's surface — scales with luminosity:

    inner edge = 0.95 × √L AU
    outer edge = 1.37 × √L AU

where L is luminosity in solar units. For a Sun-like star (L = 1), that gives roughly 0.95–1.37 AU, which neatly brackets Earth's orbit at 1.0 AU. A dim M-dwarf with L = 0.01 has its zone squeezed in to 0.095–0.137 AU, while a bright F-star pushes it outward.

This is a simplified model. Real habitable zone calculations involve albedo, greenhouse gas composition, and atmospheric pressure, but the square-root scaling captures the fundamental relationship: brighter star → wider, more distant habitable zone.

## Planet Formation

Planets are placed on orbits that follow a rough Titius-Bode-like spacing — each planet sits progressively further out, with the gap between orbits growing by a factor of about 1.4–2.2 per step. Orbital spacing also scales with stellar luminosity so that brighter stars get wider planetary systems.

What type of planet appears depends on where it formed:

**Hot zone** (inside half the habitable zone inner edge): Lava worlds, iron planets, scorched rocky bodies, and baked deserts. These are the Mercury-like hellscapes too close to their star for volatiles to survive.

**Warm zone** (between the hot zone and the habitable zone): A mix of rocky, desert, and super-Earth worlds — some may have thin atmospheres, but they're generally too warm for oceans.

**Habitable zone**: The good stuff. Rocky planets, ocean worlds, super-Earths, and even some ice or carbon worlds. This is where the simulation concentrates its most Earth-like candidates.

**Cold zone** (out to five times the habitable zone outer edge): Gas giants and ice giants dominate, alongside icy bodies — the Jupiter-and-Saturn belt.

**Far outer**: Ice giants, gas giants, ice worlds, rogue planets, and the occasional carbon world drifting in the deep cold.

## Planetary Properties

Once a planet's type is picked, the simulation generates its physical properties.

**Mass** is sampled from a type-specific range. Gas giants range from 10 to 4,000 Earth masses. Rocky planets go from 0.01 to 2. Super-Earths bridge the gap at 1.5–10.

**Radius** follows a power-law mass-radius relationship. For rocky worlds, radius scales as mass^0.27 — roughly consistent with what we observe for exoplanets. A 2-Earth-mass rocky planet comes out at about 1.2 Earth radii. Gas and ice giants use a much shallower exponent (0.06) because past a certain point, adding mass compresses the planet rather than inflating it. A gas giant's baseline is 11 Earth radii (Jupiter-scale), while ice giants start at 4.

**Orbits** obey Kepler's third law: orbital period = √(a³/M★) years, where a is the semi-major axis in AU and M★ is the star's mass in solar masses. Most orbits are fairly circular (eccentricity 0–0.3), but about 5% of planets get eccentric orbits up to 0.8 — think Pluto-like elongated paths.

**Surface temperature** comes from the energy the planet receives from its star. The equilibrium temperature formula is T = 278 × (flux)^0.25 K, where flux is the stellar luminosity divided by the orbital distance squared. This gives Earth about 278 K (5°C) as a baseline — the real average is higher due to our greenhouse effect, which the simulation also models. For planets with atmosphere pressure above 0.1 atm, a greenhouse correction warms the surface: T_surface = T_eq × (1 + 0.1 × ln(1 + pressure)). A Venus-like 90 atm atmosphere dramatically inflates the temperature.

**Axial tilt** is usually modest (0–45°) but about 10% of planets get extreme tilts up to 180° — essentially spinning upside-down like Uranus.

## Habitability

The simulation scores every planet on a 0-to-1 habitability index, combining five weighted factors:

- **Temperature (30%)**: Peaks at 288 K (15°C, Earth's average). Falls off linearly, reaching zero below 200 K or above 340 K.
- **Atmosphere (20%)**: Full score for pressures between 0.1 and 5 atm. Too thin means no liquid water; too thick means a crushing, overheated surface.
- **Water (20%)**: More water coverage is better. Ocean worlds max this out.
- **Magnetic field (15%)**: A field strength above 0.1 (Earth = 1.0) gives full marks. Without it, solar wind strips away the atmosphere over time.
- **Mass (15%)**: The sweet spot is 0.3–5 Earth masses. Too light and the planet can't hold an atmosphere. Too heavy and surface gravity becomes punishing.

An Earth twin — 288 K, 1 atm, 70% water, strong magnetic field, 1 Earth mass — scores close to 1.0. A Mars-like body (thin atmosphere, cold, no field) might score 0.1–0.2.

## The Galaxy

Systems aren't scattered randomly. The simulation generates a four-arm logarithmic spiral galaxy, modelling the Milky Way's structure.

Each arm follows a curve defined by a pitch angle of about 12.6°. Star density peaks along the arm centreline and falls off as a Gaussian with distance from it. Between the arms, density drops to about 15% of the arm peak — there are still stars in the gaps, just fewer. A dense core region fills the innermost 100 light-years. Radially, density decays exponentially with a scale length of 40,000 light-years.

Vertically, the galaxy is a thin disk. Density falls off as a Gaussian with a scale height of 500 light-years — most stars cluster near the galactic plane, with very few far above or below it.

Space is divided into 100×100×100 light-year sectors. Each sector gets a system count based on its position in the spiral arms and vertical distribution, typically 5–15 systems in arm regions and fewer between arms. A sector can hold at most 30 systems.

## Energy & Propulsion

Bob starts with 1 terajoule of stored energy and 50,000 kg of hydrogen fuel. His fusion drive converts fuel to energy at 6.3×10¹⁴ joules per kilogram — a fraction of E=mc² representing realistic fusion efficiency rather than perfect matter-antimatter annihilation.

Interstellar travel costs 0.5 kg of fuel per light-year. At Bob's top speed of 0.15c, crossing a 10-light-year gap takes about 67 years and burns 5 kg of fuel. The simulation applies the Lorentz factor from special relativity: at 0.15c, time dilation is minimal (γ ≈ 1.01), but at higher speeds available through tech upgrades, the effect becomes meaningful.

In-system manoeuvres cost fuel proportional to the square root of the body's mass — a rough model of escape velocity. Landing on a 4-Earth-mass super-Earth costs twice as much fuel as landing on an Earth-mass world. Launching is more expensive than landing (15 kg base vs. 10 kg), and landing is more expensive than entering orbit (10 kg vs. 5 kg), reflecting the extra delta-v needed for each step deeper into a gravity well.

Idle systems — life support, sensors, computing — draw 1 megajoule per tick. Surveying costs 100 MJ/tick, and mining costs 500 MJ/tick. Energy management becomes a real constraint when operating far from fuel sources.

## Mining & Resources

What you can mine depends on what the planet is made of. Iron planets are rich in iron and rare earths. Ocean worlds have abundant water. Gas giants are hydrogen goldmines with useful helium-3 reserves. Carbon worlds are the place to find carbon and silicon.

Extraction rate is: yield = 10 × mining_rate × abundance × gravity_factor kg per tick.

The gravity factor is 1/√(mass), so mining on a heavy super-Earth is slower — you're fighting a deeper gravity well to lift material. A 4-Earth-mass planet halves your mining rate compared to Earth.

Exotic matter is vanishingly rare — only 0.5% of planets have any at all, and even then in tiny amounts. Finding it is a genuine discovery.

## Survey Levels

Probes can survey planets to progressive depth. Each level reveals more information but takes longer:

| Level | Ticks | What You Learn |
|-------|-------|----------------|
| 0 (Quick scan) | 10 | Basic type, mass, rough temperature |
| 1 | 25 | Atmosphere composition, water coverage |
| 2 | 50 | Resource abundances, magnetic field |
| 3 | 100 | Detailed surface mapping, habitability score |
| 4 (Deep survey) | 200 | Everything — full geological and atmospheric profile |

A full deep survey of a single planet takes 200 ticks. With a deliberation interval of 10 and a year of 365 ticks, that's over half a year committed to one body. Choosing what to survey (and when to move on) is one of the simulation's core strategic decisions.

## Hazards

Interstellar space isn't empty. Each tick of travel carries a 0.05% chance of a micrometeorite strike, dealing 0.5% hull damage. Over a 200-tick journey, you'd expect roughly one hit. Hull damage accumulates, and a probe at zero hull integrity is destroyed. Repair costs energy but no special resources — probes carry self-repair nanobots.

## Time Scale

The simulation runs at 365 ticks per year. One thousand years make an epoch (365,000 ticks). Bob starts in a single star system and can spend decades surveying and mining before setting out across the void. At 0.15c, neighbouring stars are typically a 30–60 year trip away.

## What's Real, What's Simplified

The simulation aims for plausibility rather than precision. Star distributions, habitable zone scaling, mass-radius relationships, and Kepler's laws are drawn from real astrophysics. The simplifications — uniform sampling within ranges instead of full probability distributions, a single greenhouse coefficient, no atmospheric chemistry — keep computation fast enough for a simulation that might grow to thousands of probes across thousands of star systems.

The galaxy structure is genuinely modelled on the Milky Way's four-arm spiral, and the stellar class distribution matches observed galactic surveys. If you run the simulation long enough, the statistics of your procedural galaxy should look approximately like the real thing.
