# 9. Ship Physics Data


### 9.1 Source

Impulse engine parameters extracted from readable hardpoint scripts (`ships/Hardpoints/*.py`). Mass and inertia from `GlobalPropertyTemplates.py`.

### 9.2 Impulse Engine Parameters (27 ships verified)

| Ship | MaxAccel | MaxAngularAccel | MaxAngularVelocity | MaxSpeed |
|------|----------|-----------------|--------------------|---------:|
| Sovereign | 1.60 | 0.150 | 0.300 | 7.50 |
| Vor'cha | 1.30 | 0.110 | 0.220 | 7.60 |
| Akira | 3.00 | 0.150 | 0.400 | 6.60 |
| Galaxy | 1.50 | 0.120 | 0.280 | 6.30 |
| Bird of Prey | 2.50 | 0.350 | 0.500 | 6.20 |
| Nebula | 1.40 | 0.150 | 0.250 | 6.00 |
| Peregrine | 1.40 | 0.150 | 0.300 | 6.00 |
| Keldon | 1.50 | 0.150 | 0.300 | 5.70 |
| Marauder | 1.60 | 0.190 | 0.360 | 5.50 |
| Ambassador | 1.00 | 0.110 | 0.260 | 5.50 |
| Galor | 1.50 | 0.150 | 0.300 | 5.40 |
| Card Hybrid | 1.40 | 0.160 | 0.280 | 5.40 |
| Warbird | 1.80 | 0.070 | 0.200 | 4.50 |
| E2M0 Warbird | 1.00 | 0.070 | 0.200 | 4.50 |
| Transport | 0.50 | 0.050 | 0.120 | 4.00 |
| Shuttle | 2.50 | 0.600 | 0.800 | 4.00 |
| Comm Array | 1.00 | 0.100 | 0.250 | 4.00 |
| Comm Light | 2.00 | 0.100 | 0.250 | 4.00 |
| Kessok Light | 0.80 | 0.150 | 0.280 | 3.80 |
| Kessok Heavy | 2.50 | 0.110 | 0.220 | 3.70 |
| Freighter | 0.40 | 0.010 | 0.050 | 3.00 |
| Card Freighter | 0.80 | 0.080 | 0.150 | 3.00 |
| Sunbuster | 0.20 | 0.010 | 0.150 | 3.00 |
| Escape Pod | 0.50 | 0.300 | 0.700 | 2.00 |
| Probe | 3.00 | 0.100 | 0.300 | 8.00 |
| Probe 2 | 3.00 | 0.100 | 0.300 | 8.00 |
| Kessok Mine | 0.05 | 0.400 | 0.500 | 0.10 |

GenericTemplate (MaxAccel=20.0, MaxAngularAccel=0.1, MaxAngularVelocity=0.25, MaxSpeed=20.0) is a debug/placeholder entry not used in normal gameplay.

### 9.3 Mass and Inertia (12 ships from GlobalPropertyTemplates.py)

| Ship | Mass | Rotational Inertia | Genus | Species (template) |
|------|------|--------------------|-------|-------------------|
| Ambassador | 100.0 | 100.0 | 1 (Ship) | 104 |
| Bird of Prey | 75.0 | 100.0 | 1 (Ship) | 401 |
| Marauder | 100.0 | 100.0 | 1 (Ship) | 601 |
| Nebula | 100.0 | 100.0 | 1 (Ship) | 105 |
| Warbird | 150.0 | 100.0 | 1 (Ship) | 301 |
| Shuttle | 10.0 | 10.0 | 1 (Ship) | 106 |
| Transport | 100.0 | 100.0 | 1 (Ship) | 107 |
| Kessok Light | 100.0 | 100.0 | 1 (Ship) | 502 |
| Vor'cha | 150.0 | 100.0 | 1 (Ship) | 402 |
| Fed Starbase | 1,000,000.0 | 1,000,000.0 | 2 (Station) | 701 |
| Card Starbase | 1,000,000.0 | 1,000,000.0 | 2 (Station) | 702 |
| Card Outpost | 500.0 | 100.0 | 2 (Station) | 702 |

**Ships NOT in GlobalPropertyTemplates**: Galaxy, Sovereign, Akira, Keldon, Galor, CardHybrid, KessokHeavy, Freighter, CardFreighter, and all small objects (probes, asteroids, etc.). These likely use engine default values. Exact defaults need extraction from `Appc.pyd` or further RE work.

---

