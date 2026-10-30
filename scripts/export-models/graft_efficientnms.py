#!/usr/bin/env python3
"""Replace the ONNX NonMaxSuppression tail of an open-image-models YOLOv9
end2end export with TensorRT's EfficientNMS_TRT plugin.

Upstream (ankandrew/open-image-models, `*-license-plates-end2end.onnx`) ends
in ONNX NonMaxSuppression and a single `output0 [N, 7]` tensor
(batch, x1, y1, x2, y2, class, score). The C++ YoloPlateDetector binds the
four EfficientNMS_TRT outputs the official yolov9 `--include onnx_end2end`
export produces:

    num_dets    int32 [1, 1]
    det_boxes   float [1, topk, 4]   x1, y1, x2, y2 in input-canvas pixels
    det_scores  float [1, topk]
    det_classes int32 [1, topk]

This script cuts the graph at the pre-NMS tensors (corner boxes without the
per-class offset, and the per-class sigmoid scores), attaches one
EfficientNMS_TRT node there, and drops everything downstream. The pinned
upstream weights are untouched — no .pt re-export (and no GPL training code)
is involved.

Next to the ONNX it writes `<out>.fp32_layers`: the detect-head box decode
and the EfficientNMS tail (everything from /model/model.22/Concat_2 on),
which build_engines.sh pins to FP32. In FP16 those box coordinates (canvas
values up to 608) carry 0.5 px resolution — about 1.6 px on a 1920-wide
frame — which cost up to 16 IoU points on small plates in the parity test;
the pinned tail restores FP32 box parity at no measurable latency cost.

score_threshold is deliberately below the C++ confidence_floor (0.15) so the
floor in YoloPlateDetector::Config stays the operating control.

Usage:
    python3 -I scripts/export-models/graft_efficientnms.py \\
        --onnx models/downloads/detectors/oim-yolov9-s-608/yolo-v9-s-608-license-plates-end2end.onnx \\
        --out  models/weights/yolov9_plate.onnx [--score 0.10 --iou 0.45 --topk 100]
"""

from __future__ import annotations

import argparse
import hashlib
import sys
from pathlib import Path

import onnx
from onnx import TensorProto, helper

# Pre-NMS tensors in the open-image-models end2end graph (verified for the
# yolo-v9-t-* and yolo-v9-s-608 exports, opset 17).
BOXES = "/end2end/Squeeze_output_0"    # [1, N, 4] x1y1x2y2, no class offset
SCORES = "/end2end/Slice_4_output_0"   # [1, N, C] per-class sigmoid scores
FP32_FROM = "/model/model.22/Concat_2"  # first node of the head's box/score decode


def graft(model: onnx.ModelProto, score: float, iou: float, topk: int) -> onnx.ModelProto:
    g = model.graph
    produced = {o for n in g.node for o in n.output}
    for t in (BOXES, SCORES):
        if t not in produced:
            sys.exit(f"tensor {t!r} not in graph — not an open-image-models end2end export?")
    if not any(n.op_type == "NonMaxSuppression" for n in g.node):
        sys.exit("graph has no NonMaxSuppression node — already grafted?")

    nms = helper.make_node(
        "EfficientNMS_TRT", [BOXES, SCORES],
        ["num_dets", "det_boxes", "det_scores", "det_classes"],
        name="/end2end/EfficientNMS_TRT", domain="TRT",
        plugin_version="1", background_class=-1, max_output_boxes=topk,
        score_threshold=score, iou_threshold=iou, score_activation=0, box_coding=0,
    )
    outputs = [
        helper.make_tensor_value_info("num_dets", TensorProto.INT32, [1, 1]),
        helper.make_tensor_value_info("det_boxes", TensorProto.FLOAT, [1, topk, 4]),
        helper.make_tensor_value_info("det_scores", TensorProto.FLOAT, [1, topk]),
        helper.make_tensor_value_info("det_classes", TensorProto.INT32, [1, topk]),
    ]

    # Keep only the nodes the new outputs depend on.
    producer = {o: n for n in g.node for o in n.output}
    keep, stack = set(), [BOXES, SCORES]
    while stack:
        t = stack.pop()
        n = producer.get(t)
        if n is None or id(n) in keep:
            continue
        keep.add(id(n))
        stack.extend(i for i in n.input if i)
    nodes = [n for n in g.node if id(n) in keep] + [nms]
    used = {i for n in nodes for i in n.input}
    inits = [t for t in g.initializer if t.name in used]

    new_graph = helper.make_graph(nodes, g.name, list(g.input), outputs, inits)
    opsets = [o for o in model.opset_import if o.domain not in ("TRT",)]
    opsets.append(helper.make_opsetid("TRT", 1))
    out = helper.make_model(new_graph, opset_imports=opsets, producer_name="gate-automation graft")
    out.ir_version = model.ir_version
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--onnx", required=True, type=Path)
    ap.add_argument("--out", required=True, type=Path)
    ap.add_argument("--score", type=float, default=0.10)
    ap.add_argument("--iou", type=float, default=0.45)
    ap.add_argument("--topk", type=int, default=100)
    a = ap.parse_args()

    model = onnx.load(str(a.onnx))
    out = graft(model, a.score, a.iou, a.topk)
    a.out.parent.mkdir(parents=True, exist_ok=True)
    onnx.save(out, str(a.out))
    names = [n.name for n in out.graph.node if n.op_type != "Constant"]
    tail = names[names.index(FP32_FROM):]
    a.out.with_name(a.out.name + ".fp32_layers").write_text("\n".join(tail) + "\n")
    digest = hashlib.sha256(a.out.read_bytes()).hexdigest()
    shape = [d.dim_value for d in out.graph.input[0].type.tensor_type.shape.dim]
    print(f"wrote {a.out}  input {out.graph.input[0].name}{shape}  "
          f"nodes {len(model.graph.node)} -> {len(out.graph.node)}")
    print(f"fp32 tail: {len(tail)} layers -> {a.out.name}.fp32_layers")
    print(f"sha256 {digest}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
