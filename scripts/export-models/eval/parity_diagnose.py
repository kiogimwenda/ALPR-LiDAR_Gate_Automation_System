#!/usr/bin/env python3
"""Separate the causes of detector parity misses (TensorRT engine vs ORT).

For each image whose C++ detector input tensor was dumped by
`alpr_bench --dump-dir D --dump-ids ...` (raw float32 1x3xHxW):

  preprocessing   C++ letterbox tensor vs the Python letterbox tensor
                  (max / mean |difference|, in 0..255 pixel units)
  TRT numerics    ORT on the original upstream ONNX fed the *C++ tensor*
                  vs the TensorRT engine's detections — same input bytes,
                  so any difference is the engine's arithmetic (FP16 /
                  kernel choice / EfficientNMS vs ONNX NMS)
  preprocessing   ORT on the C++ tensor vs ORT on the Python tensor
  effect          (same network, different input)

Box differences are reported as the largest per-edge offset in INPUT-canvas
pixels (608 px canvas), so they are comparable across image sizes.

Usage:
    python3 -I scripts/export-models/eval/parity_diagnose.py \\
        --parity models/downloads/eval/cpp_parity_f010.jsonl --tensors models/downloads/eval/tensors
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import alpr_candidates as ac  # noqa: E402
from eval_candidates import load_testset  # noqa: E402
from propose_plates import iou  # noqa: E402

ONNX = ac.DL / "detectors/oim-yolov9-s-608/yolo-v9-s-608-license-plates-end2end.onnx"
SIZE = 608
THR = 0.10


def ort_on_tensor(det: ac.OimYolov9, x: np.ndarray, s: float, px: float, py: float, w: int, h: int):
    out = []
    for x1, y1, x2, y2, sc in det._raw(x):  # noqa: SLF001 — canvas-space rows
        if sc < THR:
            continue
        x1 = max(0.0, (x1 - px) / s)
        y1 = max(0.0, (y1 - py) / s)
        x2 = min(float(w), (x2 - px) / s)
        y2 = min(float(h), (y2 - py) / s)
        if x2 > x1 and y2 > y1:
            out.append((x1, y1, x2, y2, float(sc)))
    return sorted(out, key=lambda r: -r[4])


def compare(a, b, s: float) -> dict:
    """Greedy-match b to a by IoU; worst IoU, worst edge offset (canvas px), worst |dscore|."""
    used, worst_iou, worst_edge, worst_ds = set(), 1.0, 0.0, 0.0
    for g in b:
        cand = [(iou(g[:4], o[:4]), k) for k, o in enumerate(a) if k not in used]
        if not cand:
            continue
        v, k = max(cand)
        used.add(k)
        edge = max(abs(g[i] - a[k][i]) for i in range(4)) * s
        worst_iou, worst_edge = min(worst_iou, v), max(worst_edge, edge)
        worst_ds = max(worst_ds, abs(g[4] - a[k][4]))
    return {"count_a": len(a), "count_b": len(b), "worst_iou": round(worst_iou, 4),
            "worst_edge_px": round(worst_edge, 3), "worst_dscore": round(worst_ds, 4)}


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--parity", type=Path, required=True)
    ap.add_argument("--tensors", type=Path, required=True)
    ap.add_argument("--out", type=Path, default=ac.DL / "eval" / "parity_diagnose.json")
    a = ap.parse_args()
    cpp = {d["id"]: d for d in map(json.loads, a.parity.read_text().splitlines())}
    det = ac.OimYolov9(ONNX, conf=THR)
    res = {}
    for r in load_testset():
        f = a.tensors / f"{r['id']}.f32"
        if not f.exists():
            continue
        img = r["img"]
        h, w = img.shape[:2]
        canvas, s, px, py = ac.letterbox(img, SIZE)
        x_py = ac.to_chw_rgb01(canvas)
        x_cpp = np.fromfile(f, dtype=np.float32).reshape(1, 3, SIZE, SIZE)
        d = np.abs(x_cpp - x_py) * 255.0
        trt = [(*t["box"], t["score"]) for t in cpp[r["id"]]["dets"]]
        ort_cpp = ort_on_tensor(det, x_cpp, s, px, py, w, h)
        ort_py = ort_on_tensor(det, x_py, s, px, py, w, h)
        res[r["id"]] = {
            "canvas_max_abs_px": round(float(d.max()), 3),
            "canvas_mean_abs_px": round(float(d.mean()), 5),
            "canvas_pixels_differing": int((d > 0.5).sum()),
            "trt_vs_ort_same_tensor": compare(ort_cpp, trt, s),
            "ort_cpp_tensor_vs_ort_py_tensor": compare(ort_py, ort_cpp, s),
            "trt_vs_ort_py_tensor": compare(ort_py, trt, s),
        }
    a.out.write_text(json.dumps(res, indent=1, default=float))
    hdr = f"{'id':6s} {'canvas max/mean px':>18s} | {'TRT vs ORT same tensor':>34s} | {'ORT: C++ vs Py tensor':>34s}"
    print(hdr)
    for k, v in res.items():
        t, p = v["trt_vs_ort_same_tensor"], v["ort_cpp_tensor_vs_ort_py_tensor"]
        print(f"{k:6s} {v['canvas_max_abs_px']:8.2f}/{v['canvas_mean_abs_px']:<9.4f} | "
              f"n {t['count_a']}/{t['count_b']} IoU {t['worst_iou']:.3f} edge {t['worst_edge_px']:5.2f}px "
              f"ds {t['worst_dscore']:.4f} | n {p['count_a']}/{p['count_b']} IoU {p['worst_iou']:.3f} "
              f"edge {p['worst_edge_px']:5.2f}px ds {p['worst_dscore']:.4f}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
