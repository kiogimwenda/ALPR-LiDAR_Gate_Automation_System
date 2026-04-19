# ALPR + LiDAR Automated Vehicle Gate Control System

A production-grade automated gate control system that fuses Automatic License Plate Recognition (ALPR) with 3D LiDAR sensing to authorize or deny vehicle entry. The ALPR subsystem uses YOLOv9 for plate detection and PaddleOCR for character recognition, both accelerated via TensorRT on NVIDIA GPUs. The LiDAR subsystem performs point-cloud segmentation for vehicle class identification. A custom field PCB handles gate actuation, and a real-time web dashboard provides monitoring, override controls, and latency instrumentation.

**Repository:** [github.com/kiogimwenda/ALPR-LiDAR_Gate_Automation_System](https://github.com/kiogimwenda/ALPR-LiDAR_Gate_Automation_System)

## Status

Phase 0 — Environment bootstrap and repository setup.

## License

[GPL-3.0](LICENSE) — see [ADR-000](docs/decisions/ADR-000-license.md) for rationale.
