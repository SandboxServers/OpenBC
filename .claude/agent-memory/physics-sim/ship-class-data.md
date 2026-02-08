# Ship Class Data (Extracted from BC Hardpoint Scripts)

Source: `/mnt/c/users/Steve/source/projects/STBC-Dedicated-Server/reference/scripts/ships/Hardpoints/`

## Impulse Engine Parameters

| Ship             | MaxAccel | MaxAngularAccel | MaxAngularVelocity | MaxSpeed |
|------------------|----------|-----------------|--------------------|---------:|
| Sovereign        | 1.60     | 0.150           | 0.300              | 7.50     |
| Vor'cha          | 1.30     | 0.110           | 0.220              | 7.60     |
| Akira            | 3.00     | 0.150           | 0.400              | 6.60     |
| Galaxy           | 1.50     | 0.120           | 0.280              | 6.30     |
| Bird of Prey     | 2.50     | 0.350           | 0.500              | 6.20     |
| Nebula           | 1.40     | 0.150           | 0.250              | 6.00     |
| Peregrine        | 1.40     | 0.150           | 0.300              | 6.00     |
| Keldon           | 1.50     | 0.150           | 0.300              | 5.70     |
| Marauder         | 1.60     | 0.190           | 0.360              | 5.50     |
| Ambassador       | 1.00     | 0.110           | 0.260              | 5.50     |
| Galor            | 1.50     | 0.150           | 0.300              | 5.40     |
| Card Hybrid      | 1.40     | 0.160           | 0.280              | 5.40     |
| Warbird          | 1.80     | 0.070           | 0.200              | 4.50     |
| E2M0 Warbird     | 1.00     | 0.070           | 0.200              | 4.50     |
| Transport        | 0.50     | 0.050           | 0.120              | 4.00     |
| Shuttle          | 2.50     | 0.600           | 0.800              | 4.00     |
| Comm Array       | 1.00     | 0.100           | 0.250              | 4.00     |
| Comm Light       | 2.00     | 0.100           | 0.250              | 4.00     |
| Kessok Light     | 0.80     | 0.150           | 0.280              | 3.80     |
| Kessok Heavy     | 2.50     | 0.110           | 0.220              | 3.70     |
| Freighter        | 0.40     | 0.010           | 0.050              | 3.00     |
| Card Freighter   | 0.80     | 0.080           | 0.150              | 3.00     |
| Sunbuster        | 0.20     | 0.010           | 0.150              | 3.00     |
| Escape Pod       | 0.50     | 0.300           | 0.700              | 2.00     |
| Probe            | 3.00     | 0.100           | 0.300              | 8.00     |
| Probe 2          | 3.00     | 0.100           | 0.300              | 8.00     |
| Kessok Mine      | 0.05     | 0.400           | 0.500              | 0.10     |
| Generic Template | 20.00    | 0.100           | 0.250              | 20.00    |

Notes:
- MaxAngularAccel and MaxAngularVelocity appear to be in radians/second
- MaxSpeed is in BC internal units (engine units, not m/s)
- MaxAccel is in BC internal units/second^2

## Ship Mass / Rotational Inertia (from GlobalPropertyTemplates.py)

| Ship           | Mass         | RotationalInertia | Genus | Species |
|----------------|-------------|-------------------|-------|---------|
| Ambassador     | 100.0       | 100.0             | 1     | 104     |
| Bird of Prey   | 75.0        | 100.0             | 1     | 401     |
| Marauder       | 100.0       | 100.0             | 1     | 601     |
| Nebula         | 100.0       | 100.0             | 1     | 105     |
| Warbird        | 150.0       | 100.0             | 1     | 301     |
| Shuttle        | 10.0        | 10.0              | 1     | 106     |
| Transport      | 100.0       | 100.0             | 1     | 107     |
| Kessok Light   | 100.0       | 100.0             | 1     | 502     |
| Vor'cha        | 150.0       | 100.0             | 1     | 402     |
| Card Starbase  | 1000000.0   | 1000000.0         | 2     | 702     |
| Fed Starbase   | 1000000.0   | 1000000.0         | 2     | 701     |
| Card Outpost   | 500.0       | 100.0             | 2     | 702     |

Note: Galaxy, Sovereign, Akira, Keldon, Galor, etc. are NOT in GlobalPropertyTemplates.
They likely get default mass values or are set elsewhere. Need to check further.
Genus 1 = Ship, Genus 2 = Station.

## Hull HP (from Hardpoints)

| Ship           | Hull MaxCondition |
|----------------|------------------:|
| Galaxy         | 15000             |
| Sovereign      | (need to extract) |
| Bird of Prey   | 4000              |

## Shield Configuration (Bird of Prey example)

| Facing  | Max Shields | Charge/sec |
|---------|------------:|-----------:|
| Front   | 3000        | 20.0       |
| Rear    | 3000        | 8.0        |
| Top     | 3000        | 8.0        |
| Bottom  | 3000        | 3.0        |
| Left    | 3000        | 3.0        |
| Right   | 3000        | 3.0        |

ShieldGenerator MaxCondition: 5000, NormalPowerPerSecond: 180

## Shield Configuration (Galaxy)

| Facing  | Max Shields | Charge/sec |
|---------|------------:|-----------:|
| Front   | 8000        | 11.0       |
| Rear    | 4000        | 11.0       |
| Top     | 4000        | 11.0       |
| Bottom  | 4000        | 11.0       |
| Left    | 4000        | 11.0       |
| Right   | 4000        | 11.0       |

ShieldGenerator MaxCondition: 12000, NormalPowerPerSecond: 400

## Warp Core / Power (Galaxy)

| Property              | Value     |
|----------------------|----------:|
| MaxCondition          | 7000      |
| MainBatteryLimit      | 250000    |
| BackupBatteryLimit    | 80000     |
| MainConduitCapacity   | 1200      |
| BackupConduitCapacity | 200       |
| PowerOutput           | 1000      |

## Warp Core / Power (Bird of Prey)

| Property              | Value     |
|----------------------|----------:|
| MaxCondition          | 2400      |
| MainBatteryLimit      | 80000     |
| BackupBatteryLimit    | 40000     |
| MainConduitCapacity   | 470       |
| BackupConduitCapacity | 70        |
| PowerOutput           | 400       |

## Phaser Properties (Sovereign example)

| Property            | Value   |
|--------------------|--------:|
| MaxCondition        | 1000    |
| MaxCharge           | 5.0     |
| MaxDamage           | 300.0   |
| MaxDamageDistance    | 70.0    |
| MinFiringCharge     | 3.0     |
| NormalDischargeRate  | 1.0     |
| RechargeRate        | 0.08    |

## Torpedo Properties (Galaxy example)

| Property            | Value   |
|--------------------|--------:|
| MaxCondition        | 2400    |
| ImmediateDelay      | 0.25s   |
| ReloadDelay         | 40.0s   |
| MaxReady            | 1       |
| DamageRadiusFactor  | 0.2     |

## Pulse Weapon Properties (Bird of Prey - Disruptor Cannons)

| Property            | Value   |
|--------------------|--------:|
| MaxCondition        | 600     |
| MaxCharge           | 3.8     |
| MaxDamage           | 200.0   |
| MaxDamageDistance    | 100.0   |
| MinFiringCharge     | 3.6     |
| NormalDischargeRate  | 1.0     |
| RechargeRate        | 0.4     |
| CooldownTime        | 0.2s    |
