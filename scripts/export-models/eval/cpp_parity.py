#!/usr/bin/env python3
"""Score the C++ alpr_bench output against the Kenya test set and ONNX Runtime.

1. Detector parity: the grafted EfficientNMS_TRT TensorRT engine (C++,
   alpr_bench --detector-conf-floor 0.10) vs ONNX Runtime on the ORIGINAL
   upstream ONNX (ONNX NonMaxSuppression), both cut at score 0.10. Per image:
   same detection count, each C++ box matched to an ORT box
   and, per matched box, every edge within EDGE_TOL_PX canvas pixels and the
   score within SCORE_TOL (see the constants below for the measured basis).
2. End to end: C++ readings (box + raw/normalised text) matched to the
   ground truth exactly as eval_candidates.py matches Python detections
   (IoU >= 0.5, greedy by detector score), scored with the same exact-match
   and CER definitions, next to the Python number for the same pair.
3. Latency: per-frame wall time of detect() and of the full process().

Usage:
    python3 -I scripts/export-models/eval/cpp_parity.py \\
        --parity models/downloads/eval/cpp_parity_f010.jsonl \\
        --e2e models/downloads/eval/cpp_e2e.jsonl [--out models/downloads/eval/cpp_parity.json]
"""

from __future__ import annotations

import argparse
import json
import statistics
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import alpr_candidates as ac  # noqa: E402
from eval_candidates import cer, kenya_normalise, load_testset, match  # noqa: E402
from propose_plates import iou  # noqa: E402


def read_jsonl(path: Path) -> dict[str, dict]:
    return {d["id"]: d for d in map(json.loads, path.read_text().splitlines())}


# Parity tolerance for FP16 engines (RESULTS.md "Detector parity"). On the
# SAME input tensor (alpr_bench --dump-dir; parity_diagnose.py) the engine and
# ORT differ by <= 0.2 canvas px per edge on 15/16 sampled images and 1.28 px
# at worst, with |dscore| <= 0.0101. Plates are often only 5-15 px wide on the
# 608 px canvas, so a fixed IoU bar fails on sub-pixel jitter; the bar is
# therefore per box edge, in input-canvas pixels.
EDGE_TOL_PX = 1.5
SCORE_TOL = 0.02


def parity(rows, cpp: dict[str, dict], thr: float = 0.10, size: int = 608) -> dict:
    det = ac.OimYolov9(ac.DL / "detectors/oim-yolov9-s-608/yolo-v9-s-608-license-plates-end2end.onnx",
                       conf=thr)
    n_img = ok_img = n_box = iou95 = 0
    worst_iou, worst_ds, worst_edge, fails = 1.0, 0.0, 0.0, []
    edges = []
    for r in rows:
        h, w = r["img"].shape[:2]
        scale = min(size / w, size / h)
        ref = det(r["img"])
        got = [(*d["box"], d["score"]) for d in cpp[r["id"]]["dets"]]
        n_img += 1
        good = len(ref) == len(got)
        used = set()
        for g in got:
            best, bi = 0.0, -1
            for k, o in enumerate(ref):
                if k not in used and (v := iou(g[:4], o[:4])) > best:
                    best, bi = v, k
            if bi < 0:
                good = False
                continue
            used.add(bi)
            n_box += 1
            ds = abs(g[4] - ref[bi][4])
            edge = max(abs(g[i] - ref[bi][i]) for i in range(4)) * scale
            edges.append(edge)
            iou95 += best >= 0.95
            worst_iou, worst_ds, worst_edge = min(worst_iou, best), max(worst_ds, ds), max(worst_edge, edge)
            good &= edge <= EDGE_TOL_PX and ds <= SCORE_TOL
        ok_img += good
        if not good:
            fails.append({"id": r["id"], "ort": [round(float(o[4]), 4) for o in ref],
                          "trt": [round(g[4], 4) for g in got]})
    e = sorted(edges)
    return {"images": n_img, "images_pass": ok_img, "boxes_matched": n_box,
            "boxes_iou_ge_0.95": iou95, "worst_iou": worst_iou, "worst_score_diff": worst_ds,
            "worst_edge_px": worst_edge, "edge_px_p50": e[len(e) // 2], "edge_px_p99": e[int(0.99 * len(e))],
            "tolerance": {"edge_px": EDGE_TOL_PX, "score": SCORE_TOL}, "failures": fails}


def e2e(rows, cpp: dict[str, dict]) -> dict:
    gts, raw, norm = [], [], []
    tags = []
    for r in rows:
        reads = cpp[r["id"]]["readings"]
        dets = [(*x["box"], x["det_score"], x) for x in reads]
        pairs, _ = match(dets, r["plates"], 0.5)
        for k, p in enumerate(r["plates"]):
            if p["status"] != "ok":
                continue
            gts.append(p["text"])
            tags.append("2line" in p["tags"])
            got = pairs.get(k)
            raw.append(got[5]["raw"] if got else "")
            norm.append(got[5]["text"] if got else "")
    n = len(gts)
    out = {
        "n": n,
        "exact_raw": sum(a == b for a, b in zip(raw, gts)) / n,
        "exact_norm": sum(a == b for a, b in zip(norm, gts)) / n,
        "cer_raw": statistics.mean(cer(a, b) for a, b in zip(raw, gts)),
        "cer_norm": statistics.mean(cer(a, b) for a, b in zip(norm, gts)),
        # the C++ normaliser must agree with the Python reference on every string
        "normaliser_mismatches": sum(kenya_normalise(a) != b for a, b in zip(raw, norm) if a),
    }
    for lab, sel in (("1line", False), ("2line", True)):
        idx = [i for i, t in enumerate(tags) if t == sel]
        out[f"exact_norm_{lab}"] = sum(norm[i] == gts[i] for i in idx) / len(idx)
    return out


def latency(cpp: dict[str, dict]) -> dict:
    det = [d["det_ms"] for d in cpp.values()]
    frm = [d["frame_ms"] for d in cpp.values()]
    q = lambda v, p: sorted(v)[min(len(v) - 1, int(p * len(v)))]  # noqa: E731
    return {"det_ms_median": statistics.median(det), "frame_ms_median": statistics.median(frm),
            "frame_ms_p95": q(frm, 0.95), "frame_ms_max": max(frm)}


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--parity", type=Path, required=True)
    ap.add_argument("--e2e", type=Path, required=True)
    ap.add_argument("--out", type=Path, default=ac.DL / "eval" / "cpp_parity.json")
    a = ap.parse_args()
    rows = load_testset()
    res = {"parity": parity(rows, read_jsonl(a.parity)), "e2e": e2e(rows, read_jsonl(a.e2e)),
           "latency": latency(read_jsonl(a.e2e))}
    py = json.loads((ac.DL / "eval" / "results.json").read_text())["e2e"]
    res["python_e2e"] = py.get("oim-yolov9-s-608@0.15 + fpo-cct-s-v2-global/native")
    a.out.write_text(json.dumps(res, indent=1, default=float))
    p = res["parity"]
    print(f"parity (count equal, edge <= {EDGE_TOL_PX} canvas px, |dscore| <= {SCORE_TOL}): "
          f"{p['images_pass']}/{p['images']} images, {p['boxes_matched']} boxes; "
          f"edge px p50 {p['edge_px_p50']:.3f} p99 {p['edge_px_p99']:.3f} max {p['worst_edge_px']:.3f}; "
          f"worst |dscore| {p['worst_score_diff']:.4f}; "
          f"(IoU >= 0.95 on {p['boxes_iou_ge_0.95']}/{p['boxes_matched']} boxes, worst {p['worst_iou']:.4f})")
    for f in p["failures"]:
        print("   ", f)
    e, q = res["e2e"], res["python_e2e"]
    print(f"e2e C++: raw {e['exact_raw']:.3f} norm {e['exact_norm']:.3f} cer {e['cer_raw']:.3f} "
          f"(1-line {e['exact_norm_1line']:.3f}, 2-line {e['exact_norm_2line']:.3f}, "
          f"normaliser mismatches {e['normaliser_mismatches']})")
    if q:
        print(f"e2e Py : raw {q['exact_raw']:.3f} norm {q['exact_norm']:.3f} cer {q['cer_raw']:.3f}")
    print("latency:", {k: round(v, 2) for k, v in res["latency"].items()})
    return 0


if __name__ == "__main__":
    sys.exit(main())
