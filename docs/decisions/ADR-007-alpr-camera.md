# ADR-007: ALPR Camera Choice

## Status
Superseded by [ADR-012](ADR-012-fusion-sensing-hardware.md) for
production sensing hardware; retained for the prototype/pilot BOM.

## Date
2026-04-19

## Context

The ALPR camera captures vehicle front/rear plates for the detection + OCR pipeline. Requirements:
- Resolution: 1080p minimum, 4MP preferred for distant plates
- Frame rate: ≥15 fps (vehicles approach at <20 km/h in residential estates)
- IR illumination: required for night operation (plates are retroreflective)
- Global shutter preferred (eliminates rolling shutter distortion on moving vehicles)
- Interface: Ethernet (RTSP/ONVIF) or USB
- Weatherproof: IP66+ for outdoor mounting
- Budget: ≤KES 15,000 (~USD 115) for prototype, ≤KES 50,000 (~USD 385) for production

### Options Evaluated — Prototype Tier (Performance per KES)

1. **Hikvision DS-2CD2043G2-I** (4MP bullet, IR)
   - 4MP (2560×1440), 30 fps
   - IR range 40m, WDR 120dB
   - ONVIF/RTSP over Ethernet (PoE)
   - IP67, -40°C to 60°C
   - Rolling shutter (acceptable at gate speeds)
   - ~KES 8,000-12,000 in Nairobi (widely available on Luthuli Avenue)

2. **Dahua IPC-HFW2439S-SA-LED-S2** (4MP, full-color)
   - 4MP, warm LED for color night vision
   - ONVIF/RTSP, PoE
   - IP67
   - ~KES 7,000-10,000 in Nairobi
   - No dedicated IR — relies on warm light, which may cause glare on reflective plates

3. **Generic 1080p USB webcam** (e.g., Logitech C920)
   - 1080p, 30 fps
   - No IR, not weatherproof
   - USB interface — simple integration with ESP32-S3 USB OTG
   - ~KES 3,000-5,000
   - Indoor only — unusable for production

### Options Evaluated — Production Tier

1. **Hikvision DS-2CD7A46G0/P-IZHSY** (ANPR-specific)
   - Purpose-built for ANPR
   - 4MP, global shutter option available
   - IR supplement LED, motorized varifocal lens
   - Built-in ANPR firmware (not needed — we run our own pipeline)
   - ~KES 35,000-50,000

2. **FLIR/Teledyne BFS-U3-50S5C** (industrial global shutter)
   - 5MP Sony IMX250 sensor, global shutter
   - USB3 interface, SDK for Linux
   - No IR, no weatherproofing (requires enclosure)
   - ~KES 40,000-60,000
   - Best image quality for moving vehicles

3. **Basler ace 2 a2A2590-22gcBAS** (industrial GigE)
   - 5MP, global shutter, GigE Vision
   - IP30 (needs enclosure)
   - ~KES 50,000-70,000
   - Overkill for gate speeds <20 km/h

### Ranking

**Prototype: Hikvision DS-2CD2043G2-I is best** — widely available in Nairobi (Luthuli Avenue, every CCTV shop carries it), built-in IR for night, IP67, PoE simplifies cabling, 4MP is ample for plates at 5-10m, and ONVIF/RTSP makes integration trivial. Rolling shutter is acceptable at residential gate approach speeds (<10 km/h).

**Production: Hikvision ANPR camera (DS-2CD7A46G0/P) is best** — motorized lens handles variable mounting distances, dedicated IR illuminator for plates, proven in ANPR deployments. The FLIR/Basler industrial cameras are better sensors but require an enclosure, separate IR, and cost more for marginal benefit at gate speeds.

## Decision

- **Prototype:** Hikvision DS-2CD2043G2-I 4MP bullet (~KES 10,000)
- **Production:** Hikvision DS-2CD7A46G0/P-IZHSY ANPR camera (~KES 40,000)

## Consequences

- Camera driver implements RTSP client (via OpenCV `VideoCapture` with GStreamer backend)
- ONVIF used for camera discovery and configuration (exposure, gain, ROI)
- PoE simplifies field wiring (single Ethernet cable for power + data)
- Plate detection pipeline tuned for 4MP input resolution
- Night mode: IR LEDs activate automatically; OCR model trained on IR-lit plate images
