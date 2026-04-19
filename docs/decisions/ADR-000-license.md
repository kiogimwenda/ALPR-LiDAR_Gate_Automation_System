# ADR-000: Project License

## Status
Accepted

## Date
2026-04-19

## Context

This project integrates YOLOv9, which is released under the GNU General Public License v3.0 (GPL-3.0). The GPL-3.0 is a strong copyleft license that requires derivative works to also be distributed under GPL-3.0 or a compatible license.

Options considered:
1. **MIT License** — maximally permissive, allows proprietary use. However, linking with or distributing GPL-3.0 code (YOLOv9 model export tooling) may create a license conflict if interpreted as a combined work.
2. **AGPL-3.0** — compatible with GPL-3.0 but adds network-use copyleft (any interaction over a network triggers source disclosure). Unnecessarily restrictive for a gate automation system.
3. **GPL-3.0** — directly compatible with YOLOv9's license. Allows free use, modification, and distribution provided source is shared under the same terms.

## Decision

Use **GPL-3.0** for the project to maintain full compatibility with YOLOv9's license.

**Mitigating factor:** The actual runtime inference uses exported TensorRT engines (ONNX intermediate format), not the YOLOv9 Python code directly. The YOLOv9 repo is used only as export tooling. ADR-010 will explore whether this constitutes a "derivative work" and whether a license change to MIT is legally defensible for the inference-only C++ code.

## Consequences

- All source code in this repository must be made available under GPL-3.0 to anyone who receives a binary distribution.
- Commercial deployment is allowed, but customers who receive the software can request source code.
- Contributors must agree to GPL-3.0 terms.
- If ADR-010 concludes that the TRT-engine-only path is not a derivative work of YOLOv9, the project could relicense the non-YOLOv9 components under MIT in a future version.
