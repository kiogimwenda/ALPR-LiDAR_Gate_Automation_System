# LiDAR path — requirements, design, commissioning

The LiDAR half of the fusion input: point clouds in, `VehicleDetection`s
out (class, confidence, 3D box, velocity, track id, point count), attached
to every plate frame `gate-vision` submits. Code: `server/vision/` —
`include/vision/lidar/*`, `src/lidar/*`, `vision/fused_source.hpp`.

## Requirements

| # | Requirement | Source | Verified by |
|---|---|---|---|
| L1 | Detect vehicles from 0.5 m to 20 m; gate zone 5–15 m | ADR-006 | `lidar_detector_test`: sedan at 5 m; sweep at 5–12 m (front distance) |
| L2 | Tell a car from an SUV at 10 m | ADR-006 | `car vs SUV is distinguished at 10 m`; sweep below |
| L3 | Classify car / SUV / pickup / van / truck / pedestrian / cyclist / motorcycle | Phase 1 decision 2, proto `VehicleClass` | `vans and trucks…`, `dimension envelopes…` |
| L4 | Report position, extent, heading, velocity and a stable track id | proto `VehicleDetection` | `an approaching car keeps one track and a sensible velocity` |
| L5 | Report only the gate's trigger zone; ignore the next lane and fixed objects | diagram 03 | `only the trigger zone is reported`, `exclude zones mask fixed objects` |
| L6 | Keep a pedestrian beside a car separate from it | fusion: the plate belongs to the vehicle | `a pedestrian beside a car is a separate object` (+ fusion ranks plate-bearing classes first) |
| L7 | `update()` ≤ 20 ms at p99 for a 25k-point window on one core | camera ticks every 200 ms (`CameraSource` default); the LiDAR must never be the bottleneck | `update() meets the 20 ms latency budget`; `lidar-bench` |
| L8 | Pair LiDAR and camera by timestamp; drop pairs further apart than 300 ms | ADR-012 (discrete-sensor pairing) | `FusedSource attaches LiDAR vehicles by timestamp` |
| L9 | Fail closed: a dead or silent LiDAR stops `gate-vision` with a non-zero exit (systemd restarts it) | same policy as the camera | `a silent or ended LiDAR is a fault…` |
| L10 | Replay recorded clouds and run synthetic scenes | Phase 1 decision 10 | PCD round-trip tests; `e2e_lidar_stack` |

Measured on the reference dev box (Debug build, one core): p99 ≈ 2 ms at
the L1's 21,600 points/s; ≈ 2–4 ms on 25k-point windows (L7 budget 20 ms).
`lidar-bench` prints min / p50 / p99 / max and exits non-zero past budget.

Classification sweep (`test_vision "[.sweep]"`: 30 random scan patterns
per cell, 2 s of data, pillar mount; correct class / 30):

| front at | Axio (sedan) | Camry (sedan) | RAV4 (SUV) | Prado (SUV) | Hiace (van) |
|---|---|---|---|---|---|
| 5 m | 30 | 30 | 27 | 30 | 30 |
| 8 m | 30 | 30 | 30 | 30 | 30 |
| 10 m | 30 | 30 | 29 | 30 | 30 |
| 12 m | 30 | 30 | 29 | 30 | 30 |

Every vehicle was detected in every run. The RAV4 (1.69 m tall) sits
closest to the sedan/SUV height boundary and is the only miss. These are
simulated scans; the bench plan validates them on the real sensor.

## Pipeline

1. **Transform & crop** — sensor → gate frame with the site's mounting
   pose; sensor-frame range gate; crop to the tracking zone; drop
   commissioning `exclude_zones`.
2. **Accumulate** a 400 ms sliding window. The L1 delivers ~21,600
   points/s over a 360° × 90° dome; one 100 ms batch is too sparse to
   outline a car at 10 m.
3. **Ground** — RANSAC plane on low points (slope ≤ 8.5°), least-squares
   refined, EMA-smoothed. Absorbs a few degrees of bracket error.
4. **Cluster** in a 0.2 m bird's-eye grid. The bridged gap grows with
   range like the point spacing does: `max(0.4 m, 2.5 · range · angular
   spacing)`, the spacing taken from the window's real point count.
5. **Box** — lane-aligned by default (a pillar sensor sees an L-shaped
   front + side + roof whose PCA heading is noise); height = 98th
   percentile above ground.
6. **Track** the near face (the end nearest the gate — the one the
   sensor sees best) with an alpha-beta filter; dimensions grow fast and
   shrink slowly; motion smear subtracted along the heading; 500 ms coast
   for briefly occluded objects.
7. **Classify** from tracked dimensions against per-class envelopes
   (Kenyan reference models). Height is two-sided evidence; length and
   width are lower bounds (the far end is often hidden). Priors break
   ties. Confidence = class probability × fit quality × evidence (hits,
   points).

Box semantics: the reported box starts at the tracked near face and
extends by the tracked length; length is a **lower bound** of the true
vehicle length (from the pillar the low boot is barely seen).

## Sensors

| `--lidar` | Driver |
|---|---|
| `unitree-serial:/dev/ttyUSB0` | Unitree L1 over its USB-serial link (2 Mbaud) — the L1's native interface |
| `unitree-udp:10.10.10.10:5001@10.10.10.100:5000` | Unitree L1 through Unitree's serial-to-UDP adapter board |
| `pcd:<dir>` | replay `.pcd` files (ascii/binary) in name order, `--pcd-rate-hz` |
| `synthetic:<scene.json>` | ray-cast simulator (`vision/lidar/synthetic_source.hpp`) |

The Unitree driver links Unitree's prebuilt `unilidar_sdk` (BSD-3-Clause):

```bash
git clone https://github.com/unitreerobotics/unilidar_sdk ~/unilidar_sdk
cmake --preset release -DUNITREE_LIDAR_SDK_DIR=$HOME/unilidar_sdk/unitree_lidar_sdk
# (gate-vision.service already adds the dialout group for /dev/ttyUSB*)
```

> **Correction to hardware guide 03:** the L1 does not speak Ethernet on
> its own. It connects over USB-serial; Ethernet needs Unitree's
> serial-to-UDP adapter. The "192.168.1.100 / UDP 6001" settings in
> earlier drafts were wrong.

`--record-lidar <dir>` saves every raw batch for later `pcd:` replay.

## Commissioning

1. Mount the L1 on the pillar ~1.2 m up, dome axis along the lane and
   tilted ~20° down. Measure its position in the gate frame (origin on
   the ground at the gate centreline, +X out along the approach lane,
   +Y left looking out, +Z up).
2. Write `/etc/gate/lidar.json` (schema: `vision/lidar/lidar_config.hpp`;
   unknown keys are rejected):

   ```json
   {
     "mount": {"x": 0.0, "y": -2.0, "z": 1.2, "roll": 0, "pitch": 1.92, "yaw": 0},
     "trigger_zone": {"x_min": 1.0, "x_max": 12.0, "y_min": -2.5, "y_max": 2.5},
     "exclude_zones": [{"x_min": 2.3, "x_max": 3.7, "y_min": 1.3, "y_max": 2.7, "z_min": -1, "z_max": 3}]
   }
   ```

   `pitch` 1.92 rad = 110°: the L1's +Z axis rotated to point out along
   +X and 20° below the horizon.
3. Record an empty driveway (`--record-lidar`) and confirm nothing is
   reported; add an `exclude_zones` box over anything fixed that is.
4. Drive a known sedan and SUV up; check `gate-vision`'s decision log
   (`class=… vehicles=…`) and tune `trigger_zone`.
5. Production: `SOURCE_ARGS=--camera … --lidar unitree-serial:/dev/ttyUSB0
   --lidar-config /etc/gate/lidar.json` in `gate-vision.env`.

## Fusion policy

`gate-server` blends ALPR and LiDAR confidence 50/50 with a 0.7 floor by
default, so a plate needs a LiDAR-confirmed vehicle. A site with no LiDAR
must opt in with `FUSION_ARGS=--plate-only` (allowlist class restrictions
then cannot be enforced). With several objects in a frame the fusion
engine prefers plate-bearing classes over pedestrians and cyclists.

## Not in scope (yet)

* Safety-zone monitoring during gate motion — the IR safety beam and
  firmware interlock remain the safety function; LiDAR is not rated for it.
* A learned (PointNet-class) classifier — diagram 03's neural branch.
  The envelope classifier meets L2/L3 in simulation; revisit with
  recorded site data.
* LIVOX Mid-360 / ADR-012 fusion-unit drivers — add a `CloudSource`.
