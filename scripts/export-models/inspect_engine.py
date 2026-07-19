#!/usr/bin/env python3
"""Print the I/O tensor signatures of a serialized TensorRT engine (.plan).

Used to verify that a converted engine exposes the tensor names the C++ side
binds against — the YOLOv9 detector defaults to
{images, num_dets, det_boxes, det_scores, det_classes} and the PaddleOCR
recognizer to {x, softmax_2.tmp_0}. If an upstream re-export renames anything,
this script surfaces the mismatch in seconds without booting the full server.

Usage:
    python inspect_engine.py path/to/engine.plan [--verbose]
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

import tensorrt as trt


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("plan", help="Path to the .plan engine file.")
    p.add_argument("-v", "--verbose", action="store_true", help="TRT INFO logging.")
    args = p.parse_args()

    plan_path = Path(args.plan).resolve()
    if not plan_path.is_file():
        sys.exit(f"engine not found: {plan_path}")

    sev = trt.Logger.INFO if args.verbose else trt.Logger.WARNING
    runtime = trt.Runtime(trt.Logger(sev))
    engine = runtime.deserialize_cuda_engine(plan_path.read_bytes())
    if engine is None:
        sys.exit(f"failed to deserialize engine: {plan_path}")

    print(f"Engine: {plan_path}")
    print(f"  device memory required: {engine.device_memory_size / (1 << 20):.1f} MiB")
    print(f"  optimization profiles : {engine.num_optimization_profiles}")
    print(f"  I/O tensors           : {engine.num_io_tensors}\n")

    for i in range(engine.num_io_tensors):
        name = engine.get_tensor_name(i)
        mode = engine.get_tensor_mode(name)
        shape = engine.get_tensor_shape(name)
        dtype = engine.get_tensor_dtype(name)
        kind = "INPUT " if mode == trt.TensorIOMode.INPUT else "OUTPUT"
        print(f"  [{kind}] {name}")
        print(f"           shape = {tuple(shape)}   dtype = {dtype.name}")

        # Print profile shape ranges for dynamic inputs.
        if mode == trt.TensorIOMode.INPUT:
            for prof in range(engine.num_optimization_profiles):
                lo, opt, hi = engine.get_tensor_profile_shape(name, prof)
                print(f"           profile[{prof}] min={tuple(lo)} opt={tuple(opt)} max={tuple(hi)}")
        print()

    return 0


if __name__ == "__main__":
    sys.exit(main())
