# ADR-006: LiDAR Sensor Choice

## Status
Accepted

## Date
2026-04-19

## Context

The LiDAR subsystem detects vehicle presence in the gate trigger zone, classifies vehicles by silhouette (car/SUV/truck), and monitors the gate zone during opening/closing for safety (obstacle detection). Requirements:
- 3D point cloud (not 2D scan)
- Range: 0.5m to 20m minimum (gate zone is ~5-15m)
- Angular coverage: ≥90° horizontal (gate approach lane)
- Point density sufficient to distinguish a car from an SUV at 10m
- Outdoor-rated (IP65+ for rain, dust, sunlight)
- Interface: Ethernet (UDP point cloud) or UART
- Budget: ≤KES 60,000 (~USD 460) for prototype, ≤KES 150,000 (~USD 1,150) for production

### Options Evaluated — Prototype Tier (Performance per KES)

1. **Unitree L1** — ~USD 250-350
   - 3D, 21,600 points/s, 360° × 90° FOV
   - 30m range, IP54
   - Ethernet (100 Mbps) + serial
   - Excellent price-to-performance
   - Available on AliExpress, ~KES 35,000-50,000

2. **LIVOX Mid-360** — ~USD 500-600
   - Non-repetitive scan, high density over time
   - 200m range, 360° × 59° FOV
   - Ethernet (1 Gbps)
   - IP67, automotive-grade
   - ~KES 65,000-80,000

3. **Slamtec RPLIDAR S2** — ~USD 150-200
   - **2D only** — single plane scan
   - 30m range, 360° mechanical
   - Insufficient for vehicle classification (no height data)
   - Only suitable for basic presence detection

### Options Evaluated — Production Tier

1. **LIVOX HAP** — ~USD 800-1,000
   - Solid-state, no moving parts (long lifespan)
   - 150m range, 120° × 25° FOV
   - IP67, automotive-grade
   - ~KES 105,000-130,000

2. **RoboSense Bpearl** — ~USD 1,200-1,500
   - Hemisphere 3D, 360° × 90°
   - 30m range, IP67
   - Ideal for overhead mounting above gate
   - ~KES 155,000-200,000

3. **Hesai XT32** — ~USD 2,000+
   - 32-channel mechanical, 120m range
   - Highest point density
   - Overkill for gate application

### Ranking

**Prototype: Unitree L1 is best** — lowest cost, adequate 3D coverage, Ethernet interface, available in Kenya via AliExpress. The 360° × 90° FOV is more than sufficient for a gate lane.

**Production: LIVOX Mid-360 is best** — higher point density, IP67 for outdoor durability, non-repetitive scan builds dense maps over time. The HAP is better for long-range but has narrower FOV; the Mid-360's full 360° is more versatile for gate installations.

## Decision

- **Prototype:** Unitree L1 (~KES 40,000)
- **Production:** LIVOX Mid-360 (~KES 70,000), with LIVOX HAP as alternative for specific mounting constraints

## Consequences

- LiDAR driver must support Unitree L1 protocol (Ethernet UDP point cloud)
- Production driver adds LIVOX SDK support (Ethernet, different packet format)
- HAL abstracts sensor-specific protocols behind a common `PointCloud` interface
- Mounting bracket designed for both overhead and side-mount configurations
- Safety: Unitree L1 is Class 1 laser (eye-safe); LIVOX Mid-360 is also Class 1
