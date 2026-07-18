# ADR-012: Production Sensing Hardware — Camera–LiDAR Fusion Unit

## Status
Accepted — supersedes [ADR-006](ADR-006-lidar-sensor.md) and
[ADR-007](ADR-007-alpr-camera.md) for **production** sensing hardware;
those ADRs remain in force for the prototype and pilot BOM.

## Date
2026-07-18

## Context

ADR-006 and ADR-007 selected sensing hardware as two independent
decisions: a LiDAR (Unitree L1 prototype / LIVOX Mid-360 production)
and an ALPR camera (Hikvision DS-2CD2043G2-I prototype /
DS-2CD7A46G0/P production). That split was correct for the budget and
availability constraints of the prototype, but it bakes a structural
cost into every production install: the camera and LiDAR are two
devices with two mounts, two calibrations, two clocks, and two data
paths that the server must fuse in software after the fact.

The project owner has directed that production deployments adopt a
**true camera–LiDAR fusion sensing unit** — a single device that
captures RGB and depth through hardware-synchronized (ideally
co-axial) optics. Named candidates: the Kyocera Camera-LIDAR Fusion
Sensor and an Innoviz "InnovizThree"-class unit.

Why fusion at the sensor beats software-fusing discrete sensors:

- **Hardware-synchronized frames.** The discrete stack timestamps the
  RTSP video and the UDP point cloud on arrival and pairs them by
  nearest-timestamp; camera pipeline latency and LiDAR scan period
  jitter mean a "simultaneous" pair can be tens of milliseconds apart
  — enough for a vehicle at 10 km/h to move ~10 cm between the two
  captures. A fusion unit exposes one exposure event for both
  modalities, so `DetectionFrame.capture_ts` becomes a real capture
  time instead of a reconstruction.
- **Single mount, single extrinsic calibration.** The discrete pair
  needs a per-site extrinsic calibration (camera↔LiDAR transform)
  that drifts whenever either bracket is knocked, re-aimed, or
  thermally cycled. A fusion unit is factory-aligned; there is nothing
  to calibrate in the field and nothing to drift.
- **Per-pixel depth association.** Co-axial or factory-registered
  optics give every camera pixel a depth value with no parallax
  occlusion (near objects hiding different background regions from
  each sensor). The fusion engine's plate↔vehicle association —
  currently a 3D-projection heuristic — degenerates to a lookup.
- **One cable, one enclosure, one failure domain** at the gate pillar
  instead of two weatherproofed devices plus a PoE port each.

The deployment strategy (Phase 1 decisions) is **residential first,
commercial/industrial later** — which matters below, because
fusion-class hardware pricing currently fits the second tier, not the
first.

### Requirements — Production Fusion Sensing Unit

| Requirement | Target | Rationale |
|---|---|---|
| Depth range | 0.5–20 m usable (gate zone 5–15 m) | Same envelope as ADR-006; residential approach lane |
| Horizontal FoV | ≥90° depth, camera FoV covering the plate zone at 3–10 m | Full lane coverage per ADR-006/007 |
| RGB resolution | ≥4 MP effective at plate distance | Plate legibility parity with ADR-007's Hikvision |
| Frame sync | Hardware-synchronized RGB + depth, single timestamp | The point of this ADR |
| Registration | Factory-aligned, per-pixel depth↔pixel mapping | Eliminates field extrinsic calibration |
| Day/night | IR or active illumination path for retroreflective plates | Night ALPR is non-negotiable (ADR-007) |
| Interface | Ethernet preferred (fits PoE switch topology); GMSL2/proprietary acceptable via adapter on the GPU host | Server-side capture only — the ESP32-S3 never touches sensors |
| Environmental | IP65+ (IP67 preferred), -10°C to 60°C | Outdoor pillar mount, Nairobi climate margin |
| Laser safety | Class 1 (eye-safe) | Public-facing gate approach |
| Budget | See revised production sensing budget line in Consequences | ADR-006's ceiling no longer applies |

### Options Evaluated — Production Fusion Tier

1. **Kyocera Camera-LIDAR Fusion Sensor** — pricing unpublished
   - The only announced device with **co-axial optics**: camera and
     LiDAR share one optical axis, giving genuinely parallax-free,
     per-pixel superimposed RGB+depth
     ([Kyocera press release](https://global.kyocera.com/newsroom/news/2025/000991.html),
     [Electronics Weekly](https://www.electronicsweekly.com/news/business/kyoceras-integrates-camera-and-lidar-sensor-2025-02/))
   - Proprietary MEMS mirror; claimed world's highest laser
     irradiation density (0.045°); detects a 30 cm object at 100 m
   - Target markets explicitly include **security systems** — the
     closest fit to a gate deployment of any candidate
   - **Development-stage**: demonstrated at CES 2025; Kyocera is
     "working towards early commercialization" with **no published
     FoV, interface, IP rating, ship date, or price**. Not orderable
     today; cannot be specced into a BOM yet.

2. **Innoviz InnovizThree with integrated colored camera** — pricing
   unpublished for low volumes; automotive-volume ASP trajectory known
   - Announced December 2025; the camera-fusion variant announced
     January 2026 at CES 2026 as the "first sensor-fusion colored 3D
     LiDAR and camera" — RGB **factory-aligned** to the LiDAR with
     **hardware-synchronized capture**, delivered over a **single
     integration interface**
     ([Innoviz press release](https://ir.innoviz.tech/news-events/press-releases/detail/167/innoviz-technologies-announces-first-sensor-fusion-colored),
     [Automotive World](https://www.automotiveworld.com/topics/software-defined-vehicle/innoviz-adds-camera-to-innovizthree-lidar-sensor/))
   - Not co-axial (separate factory-registered apertures), but the
     factory alignment + hardware sync deliver the same per-pixel
     association guarantee without field calibration
   - 60% smaller than InnovizTwo, >35% cheaper than InnovizTwo,
     detection range beyond 250 m — long-range automotive-grade
     performance, far beyond what a 15 m gate zone needs
   - Explicitly marketed **beyond automotive OEMs**: drones,
     micro-robotics, humanoids — i.e., Innoviz intends to sell these
     to buyers like us, unlike OEM-only automotive LiDAR programs
   - Pricing: InnovizTwo's automotive-volume ASP is under USD 500,
     and InnovizThree cuts that by >35%
     ([Design News](https://www.designnews.com/automotive-engineering/innoviz-gears-up-for-level-3-driver-assistance),
     [Yahoo Finance](https://finance.yahoo.com/news/innoviz-technologies-unveils-innovizthree-quantum-130000589.html))
     — **but those are 100k-unit OEM prices**. Low-volume evaluation
     units of automotive LiDAR of this class historically sell for
     **USD 3,000–10,000 per unit**; no sample pricing has been
     published for InnovizThree, so we carry that range as an
     estimate, explicitly marked as such. InnovizTwo is SOP-ready in
     2026 with deployments accelerating in 2027
     ([Innoviz CEO letter](https://www.stocktitan.net/news/INVZ/innoviz-highlights-breakout-2025-and-ces-2026-traction-in-ceo-letter-2hhhbaw0ru0v.html));
     InnovizThree SOP is later and unannounced.

3. **Discrete pair, software-fused (baseline)** — Hikvision
   DS-2CD2043G2-I + Unitree L1 (prototype) or Hikvision ANPR +
   LIVOX Mid-360 (production per ADR-006/007)
   - Prototype pair: ~KES 45,000 (~USD 346) combined; ADR-006/007
     production pair: ~KES 110,000 (~USD 846) combined
   - Proven, purchasable today, camera on Luthuli Avenue same-day
   - Carries the structural costs this ADR exists to remove:
     nearest-timestamp pairing, field extrinsic calibration,
     two mounts, two failure domains
   - Remains fully adequate for prototype, pilot, and — on price —
     for first-generation residential production installs

### Candidate Comparison

| | Kyocera Fusion Sensor | InnovizThree + camera | Discrete pair (baseline) |
|---|---|---|---|
| Optical architecture | Co-axial, parallax-free | Factory-registered apertures | Two devices, field-calibrated |
| Frame sync | Hardware, single unit | Hardware, single interface | Software, nearest-timestamp |
| Per-pixel depth↔RGB | Yes, by construction | Yes, factory-aligned | Approximated via extrinsics |
| Range | 30 cm object at 100 m | >250 m | 20–30 m (ample for gate) |
| Weatherproofing | Unpublished | Unpublished (automotive-grade design) | IP67 (both production units) |
| Interface | Unpublished | Single interface (details unpublished) | RTSP/ONVIF + UDP point cloud |
| Buy today? | No — pre-commercial | Samples expected post-SOP; not yet | Yes |
| Unit price | Unpublished | Est. USD 3,000–10,000 low-volume; <USD 325 at OEM volume | ~USD 846 (production pair) |
| Fit to security/gate use | Explicit target market | Marketed beyond automotive | Proven in this project |

### Ranking

**InnovizThree-class fusion module is the primary production
candidate** — it is the only announced fusion unit with a stated path
to purchasable volume hardware, explicit non-automotive availability,
and the factory-aligned + hardware-synchronized properties that
motivate this ADR. **Kyocera's co-axial sensor is the technically
ideal architecture** (true parallax-free shared optics, security
systems as a named market) and is retained as the preferred
alternative the moment Kyocera publishes specs, availability, and
pricing. The discrete pair is not a fusion candidate — it is the
baseline being superseded — but it remains the shipping stack for
prototype, pilot, and price-constrained residential installs.

## Decision

- **Production sensing = one camera–LiDAR fusion unit.** Primary
  candidate: **Innoviz InnovizThree with integrated colored camera**.
  Watch-list alternative: **Kyocera Camera-LIDAR Fusion Sensor**,
  promoted to primary if its co-axial unit commercializes with
  published specs and comparable pricing before we lock a production
  BOM.
- **Prototype and pilot builds are unchanged**: Hikvision
  DS-2CD2043G2-I + Unitree L1 per ADR-006/007, which remain in force
  for that tier.
- **Tier gating**: given current fusion-unit economics (see budget in
  Consequences), the fusion unit ships first in
  **commercial/industrial** deployments. Residential production
  installs continue on the ADR-006/007 production pair (Hikvision
  ANPR + LIVOX Mid-360, ~KES 110,000) until a fusion unit clears
  roughly that combined price point.
- Procure one evaluation unit as soon as Innoviz opens InnovizThree
  sample orders; bring-up happens behind the Phase 5.3 capture-source
  seam (below) with zero contract changes.

## Consequences

- **The software contract already absorbs this with no changes.**
  `DetectionFrame` in `shared/proto/gate_service.proto` (~line 181)
  is sensor-agnostic: it carries `plates` + `vehicles` in one
  timestamped frame regardless of how many physical devices produced
  them. A fusion unit lands as a **new capture driver on the GPU
  host** — the Phase 5.3 capture-source seam in `server/vision/` —
  with no `.proto` change and no fusion-engine change. If anything,
  the fusion engine's plate↔vehicle association gets *more* reliable
  because `capture_ts` becomes a true single-exposure timestamp.
- **Fusion sensors do not read license plates.** The unit replaces
  the *frame source*, not the *inference*: YOLOv9 plate detection +
  PaddleOCR recognition (TensorRT, per ADR-010's licensing analysis)
  keep running on the GPU host against the unit's RGB stream. The
  depth channel feeds vehicle detection/classification exactly as the
  LiDAR point cloud does today.
- **Revised production sensing budget line.** ADR-006's production
  ceiling (≤KES 150,000 / ~USD 1,150 for the LiDAR) plus ADR-007's
  (≤KES 50,000 / ~USD 385 for the camera) — combined ~KES 200,000
  (~USD 1,535) — **does not cover fusion-class hardware today**. The
  production sensing line is revised to **KES 400,000–1,300,000
  (~USD 3,000–10,000) per gate** at low-volume/evaluation pricing,
  and is accordingly **deferred to the commercial/industrial
  deployment tier**. This is consistent with the deployment strategy
  (residential first, commercial later): residential production keeps
  the ADR-006/007 pair; the budget line is revisited when
  InnovizThree volume pricing (<USD 325 at OEM scale) becomes
  accessible through distribution, at which point the fusion unit
  undercuts even the discrete production pair.
- **Zero firmware impact.** The ESP32-S3 field controller never
  touches sensors — it drives the relay/actuator, safety beam, and
  limit switches and speaks gRPC to the server (ADR-011). Sensor
  capture is entirely server-side, so this ADR changes nothing in
  `firmware/`.
- **New driver work is scoped and isolated**: one capture driver
  implementing the unit's interface (Ethernet preferred; if the
  production interface is automotive GMSL2, budget a GMSL2→PCIe
  capture card on the GPU host — add ~KES 40,000–65,000 / USD
  300–500 to the install if required).
- **Procurement risk is explicit**: both candidates are
  pre-availability as of this writing (Kyocera pre-commercial;
  InnovizThree pre-SOP). This ADR intentionally decides the
  *architecture* (fusion unit) and the *candidate order* now, and
  gates the BOM commitment on published pricing/specs. If neither
  unit is orderable when the first commercial deployment is quoted,
  the ADR-006/007 production pair ships and this ADR's hardware
  selection rolls to the next deployment.
- Weatherproofing (IP rating) and day/night ALPR performance of both
  candidates are **unpublished** — the evaluation unit must be
  validated against the Requirements table above (night
  retroreflective-plate imaging especially) before any BOM lock.
