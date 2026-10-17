#!/usr/bin/env python3
"""Convert an ONNX model to a serialized TensorRT engine (.plan).

Builds an FP16 engine by default with optional INT8 and configurable
dynamic-shape profiles. Engines are native to the GPU they are built on: the
system supports every NVIDIA RTX generation from the 20 series (Turing sm_75,
Ampere sm_86, Ada sm_89, Blackwell sm_120), and no TensorRT hardware-
compatibility level spans Turing and newer, so engines are built on the
deployment host itself (scripts/export-models/build_engines.sh does this).
`--hardware-compat ampere_plus` remains available for fleets that are all
sm_80 or newer.

Next to the .plan it writes `<plan>.meta` (build GPU, compute capability,
TensorRT version, compatibility mode); the C++ loader checks it at startup
and refuses a mismatched engine with a "rebuild on this host" message.

The companion `inspect_engine.py` reads the resulting .plan and prints its
I/O tensor signatures so the C++ side (yolo_plate_detector / paddle_ocr_recognizer)
can verify the tensor names it binds against.

Examples
--------
YOLOv9 plate detector (end2end ONNX with EfficientNMS_TRT, fixed 1x3x640x640):
    python convert_onnx_to_trt.py \\
        --onnx  models/yolov9_plate.onnx \\
        --plan  ../../server/models/yolov9_plate.plan \\
        --fp16

PaddleOCR PP-OCRv4 (variable width up to 320):
    python convert_onnx_to_trt.py \\
        --onnx  models/ppocrv4_rec.onnx \\
        --plan  ../../server/models/ppocrv4_rec.plan \\
        --fp16 \\
        --input-name x \\
        --min-shape  1x3x48x32 \\
        --opt-shape  1x3x48x160 \\
        --max-shape  1x3x48x320

Both runs print the resolved tensor shapes after build so you can copy the
right names into the C++ Config defaults if upstream renamed something.
"""

from __future__ import annotations

import argparse
import logging
import subprocess
import sys
from pathlib import Path

import tensorrt as trt

LOG = logging.getLogger("convert_onnx_to_trt")


def parse_shape(s: str) -> tuple[int, ...]:
    """Parse 'NxCxHxW' (or any 'AxBxC...') into a tuple of ints."""
    parts = [p for p in s.lower().split("x") if p]
    if not parts:
        raise argparse.ArgumentTypeError(f"empty shape: {s!r}")
    try:
        return tuple(int(p) for p in parts)
    except ValueError as exc:
        raise argparse.ArgumentTypeError(f"non-integer in shape {s!r}: {exc}") from exc


def query_gpu() -> tuple[str, int, int, int]:
    """(name, cc_major, cc_minor, total_mib) of GPU 0, via nvidia-smi."""
    out = subprocess.run(
        ["nvidia-smi", "--query-gpu=name,compute_cap,memory.total",
         "--format=csv,noheader,nounits", "--id=0"],
        check=True, capture_output=True, text=True,
    ).stdout.strip()
    name, cc, mem = (f.strip() for f in out.split(","))
    major, minor = (int(x) for x in cc.split("."))
    return name, major, minor, int(float(mem))


def write_meta(plan_path: Path, gpu: str, major: int, minor: int, compat: str) -> Path:
    """Sidecar the C++ loader checks (see inference/detail/engine_compat.hpp)."""
    meta = plan_path.with_name(plan_path.name + ".meta")
    meta.write_text(
        "# written by convert_onnx_to_trt.py — checked by the C++ engine loader\n"
        f"gpu={gpu}\n"
        f"compute_capability={major}.{minor}\n"
        f"tensorrt={'.'.join(trt.__version__.split('+')[0].split('.')[:3])}\n"
        f"hardware_compat={compat}\n"
    )
    return meta


def make_logger(verbose: bool) -> trt.Logger:
    sev = trt.Logger.INFO if verbose else trt.Logger.WARNING
    return trt.Logger(sev)


def build_engine(args: argparse.Namespace) -> bytes:
    onnx_path = Path(args.onnx).resolve()
    if not onnx_path.is_file():
        sys.exit(f"ONNX not found: {onnx_path}")

    trt_logger = make_logger(args.verbose)
    builder = trt.Builder(trt_logger)
    network = builder.create_network(0)  # explicit batch is the default in TRT 10
    parser = trt.OnnxParser(network, trt_logger)

    LOG.info("parsing ONNX: %s", onnx_path)
    with onnx_path.open("rb") as f:
        if not parser.parse(f.read()):
            for i in range(parser.num_errors):
                LOG.error("onnx-parser: %s", parser.get_error(i))
            sys.exit("ONNX parse failed")

    config = builder.create_builder_config()
    gpu, major, minor, total_mib = query_gpu()
    LOG.info("building on %s (sm_%d%d, %d MiB)", gpu, major, minor, total_mib)
    if (major, minor) < (7, 5):
        sys.exit(f"{gpu} (sm_{major}{minor}) is older than Turing; an RTX 20-series or newer GPU is required")
    # Half the card's memory, capped at 4 GiB: lets 4-6 GB RTX 20/30 cards build.
    workspace_mb = args.workspace_mb or min(4096, total_mib // 2)
    LOG.info("builder workspace: %d MiB", workspace_mb)
    config.set_memory_pool_limit(trt.MemoryPoolType.WORKSPACE, workspace_mb * (1 << 20))

    if args.fp16:
        if not builder.platform_has_fast_fp16:
            LOG.warning("platform reports no fast FP16 — building anyway")
        config.set_flag(trt.BuilderFlag.FP16)
    if args.int8:
        if not builder.platform_has_fast_int8:
            LOG.warning("platform reports no fast INT8 — INT8 will likely fall back to FP16")
        config.set_flag(trt.BuilderFlag.INT8)

    if args.hardware_compat == "ampere_plus":
        # One .plan for every sm_80+ GPU (RTX 30/40/50) at ~5-10% speed cost.
        # Never loads on Turing (RTX 20), so it cannot be built there either.
        if major < 8:
            sys.exit("--hardware-compat ampere_plus needs an sm_80+ build GPU and the result "
                     "would not run on this RTX 20-series host; use the default (none)")
        config.hardware_compatibility_level = trt.HardwareCompatibilityLevel.AMPERE_PLUS
    elif args.hardware_compat == "same_cc":
        # Same compute capability as the build GPU, any card of that generation.
        config.hardware_compatibility_level = trt.HardwareCompatibilityLevel.SAME_COMPUTE_CAPABILITY

    # Dynamic-shape profile, if any input was given a shape range.
    if args.min_shape or args.opt_shape or args.max_shape:
        if not (args.min_shape and args.opt_shape and args.max_shape):
            sys.exit("--min-shape, --opt-shape, --max-shape must all be given together")
        if not args.input_name:
            sys.exit("--input-name is required when supplying a dynamic shape profile")
        profile = builder.create_optimization_profile()
        profile.set_shape(args.input_name, args.min_shape, args.opt_shape, args.max_shape)
        config.add_optimization_profile(profile)
        LOG.info(
            "profile: %s  min=%s opt=%s max=%s",
            args.input_name,
            args.min_shape,
            args.opt_shape,
            args.max_shape,
        )

    LOG.info("building engine — this can take several minutes")
    serialized = builder.build_serialized_network(network, config)
    if serialized is None:
        sys.exit("engine build failed (build_serialized_network returned None)")
    args._built_on = (gpu, major, minor)
    return bytes(serialized)


def write_engine(blob: bytes, plan_path: Path) -> None:
    plan_path.parent.mkdir(parents=True, exist_ok=True)
    plan_path.write_bytes(blob)
    LOG.info("wrote %s (%.1f MiB)", plan_path, len(blob) / (1 << 20))


def main() -> int:
    p = argparse.ArgumentParser(
        description="Convert an ONNX model to a TensorRT .plan.",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    p.add_argument("--onnx", required=True, help="Input ONNX model.")
    p.add_argument("--plan", required=True, help="Output .plan path.")
    p.add_argument("--fp16", action="store_true", help="Enable FP16 kernels.")
    p.add_argument("--int8", action="store_true", help="Enable INT8 kernels (needs calibration).")
    p.add_argument(
        "--workspace-mb",
        type=int,
        default=0,
        help="Builder workspace memory pool, in MiB (0 = half the GPU's memory, max 4096).",
    )
    p.add_argument(
        "--hardware-compat",
        choices=("none", "same_cc", "ampere_plus"),
        default="none",
        help="none = native to this GPU (works on every RTX generation); same_cc = any GPU "
        "with this compute capability; ampere_plus = one .plan for sm_80+ (not RTX 20).",
    )
    p.add_argument("--input-name", help="Tensor name for the dynamic-shape profile.")
    p.add_argument("--min-shape", type=parse_shape, help="e.g. 1x3x48x32")
    p.add_argument("--opt-shape", type=parse_shape, help="e.g. 1x3x48x160")
    p.add_argument("--max-shape", type=parse_shape, help="e.g. 1x3x48x320")
    p.add_argument("-v", "--verbose", action="store_true", help="TRT INFO logging.")
    args = p.parse_args()

    logging.basicConfig(
        level=logging.DEBUG if args.verbose else logging.INFO,
        format="%(asctime)s %(levelname)-7s %(name)s — %(message)s",
    )

    blob = build_engine(args)
    plan_path = Path(args.plan).resolve()
    write_engine(blob, plan_path)
    gpu, major, minor = args._built_on
    LOG.info("wrote %s", write_meta(plan_path, gpu, major, minor, args.hardware_compat))

    # Print final I/O signatures so the operator can copy tensor names into the
    # C++ Config block. We re-deserialize because Builder doesn't expose them.
    runtime = trt.Runtime(make_logger(args.verbose))
    engine = runtime.deserialize_cuda_engine(blob)
    print("\nEngine I/O signatures:")
    for i in range(engine.num_io_tensors):
        name = engine.get_tensor_name(i)
        mode = engine.get_tensor_mode(name)  # INPUT or OUTPUT
        shape = engine.get_tensor_shape(name)
        dtype = engine.get_tensor_dtype(name)
        kind = "in " if mode == trt.TensorIOMode.INPUT else "out"
        print(f"  [{kind}] {name:30s} shape={tuple(shape)}  dtype={dtype.name}")

    return 0


if __name__ == "__main__":
    sys.exit(main())
