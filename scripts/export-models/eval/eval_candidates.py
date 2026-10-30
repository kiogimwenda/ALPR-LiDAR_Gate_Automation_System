#!/usr/bin/env python3
"""Phase-1 evaluation of the ALPR candidates on the hand-labelled Kenya test set.

Metrics
  detection  per detector: precision / recall at IoU>=0.5 (detections on
             'ignore' regions are neither TP nor FP), plate-found rate at
             IoU>=0.3, at the detector's operating confidence.
  OCR        per recognizer mode, on (a) oracle crops from the GT boxes and
             (b) end to end behind each detector: exact match on the raw
             string and after Kenya-format normalisation, and CER.
             Crops use the C++ pipeline's 8 % margin (AlprPipeline::Config).
  latency    ORT CUDA EP and ORT CPU EP, per frame (detector) and per crop
             (recognizer), median over the test set after warm-up.

Usage:
    python3 -I scripts/export-models/eval/eval_candidates.py [--cpu-latency-n 30]
Writes models/downloads/eval/results.json, RESULTS.md (tables) and
annotated images for the recommended pair.
"""

from __future__ import annotations

import argparse
import csv
import json
import re
import statistics
import sys
import time
from pathlib import Path

import cv2
import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import alpr_candidates as ac  # noqa: E402
from propose_plates import iou  # noqa: E402

TS = ac.DL / "kenya-testset"
OUT = ac.DL / "eval"
CROP_MARGIN = 0.08  # AlprPipeline::Config::crop_margin
# Operating points: 0.25 is the C++ YoloPlateDetector::Config::confidence_floor
# default; the second YOLO point and the RT-DETR points come from the
# threshold sweep (RT-DETR's single-class scores are compressed below ~0.15).
CONF = {"oim-yolov9-t-640": 0.25, "oim-yolov9-t-640@0.10": 0.10,
        "oim-yolov9-s-608": 0.25, "oim-yolov9-s-608@0.15": 0.15,
        "morsetech-yolov11s": 0.25,
        "rtdetrv2-r18": 0.05, "rtdetrv2-r18@0.08": 0.08, "rtdetrv2-r18/letterbox": 0.05}

# ---------------------------------------------------------------- Kenya format
# L = letter, D = digit. Civilian KAA 000A; motorcycle KMAA 000A; taxi / tuk-tuk
# KTWA 000A; GK (government) GK A 000A / GK 000A; CD (diplomatic) 00 CD 00K is
# rare and left alone; old KAA 000 / KAA 00 / KAA 0 and foreign plates
# (UG UAA 000A, TZ T000AAA, RW RAA000A) are not forced into a Kenyan template.
TEMPLATES = ["KLLDDDL", "KLLLDDDL", "GKLDDDL", "GKDDDL"]  # L letter, D digit, else literal
TO_DIGIT = {"O": "0", "Q": "0", "D": "0", "U": "0", "I": "1", "L": "1", "Z": "2",
            "S": "5", "B": "8", "G": "6", "A": "4", "T": "7"}
TO_LETTER = {"0": "O", "1": "I", "2": "Z", "5": "S", "8": "B", "6": "G", "4": "A", "7": "T"}


def kenya_normalise(s: str) -> str:
    """Positional confusable repair (0<->O, 1<->I, 8<->B, ...) against the Kenyan
    templates of the same length; the cheapest reachable template wins. Strings
    that fit no template (old KAA 000, foreign UG/TZ/RW plates) pass unchanged."""
    s = re.sub(r"[^A-Z0-9]", "", s.upper())
    best = None
    for tpl in TEMPLATES:
        if len(tpl) != len(s):
            continue
        out, cost = [], 0
        for want, c in zip(tpl, s):
            if want == "D" and not c.isdigit():
                c, cost = TO_DIGIT.get(c, ""), cost + 1
            elif want != "D" and c.isdigit():
                c, cost = TO_LETTER.get(c, ""), cost + 1
            if not c or (want not in "LD" and c != want):
                break
            out.append(c)
        else:
            if best is None or cost < best[0]:
                best = (cost, "".join(out))
    return best[1] if best else s


def cer(pred: str, gt: str) -> float:
    d = list(range(len(gt) + 1))
    for i, pc in enumerate(pred, 1):
        prev, d[0] = d[0], i
        for j, gc in enumerate(gt, 1):
            prev, d[j] = d[j], min(d[j] + 1, d[j - 1] + 1, prev + (pc != gc))
    return d[len(gt)] / max(1, len(gt))


# ---------------------------------------------------------------- helpers
MANIFEST = Path(__file__).resolve().parent / "testset" / "manifest.csv"


def load_testset():
    """Committed manifest + images fetched by fetch_testset.py."""
    rows = list(csv.DictReader(MANIFEST.open()))
    for r in rows:
        r["plates"] = json.loads(r["plates_json"])
        r["img"] = cv2.imread(str(TS / r["file"]))
    return rows


def crop(img, box, margin=CROP_MARGIN):
    """Same as detail::expand_box_with_margin + frame(roi)."""
    x1, y1, x2, y2 = box
    dx, dy = (x2 - x1) * margin, (y2 - y1) * margin
    H, W = img.shape[:2]
    x1, y1 = np.clip(x1 - dx, 0, W), np.clip(y1 - dy, 0, H)
    x2, y2 = np.clip(x2 + dx, 0, W), np.clip(y2 + dy, 0, H)
    xi, yi = int(np.floor(x1)), int(np.floor(y1))
    wi, hi = int(np.ceil(x2 - x1)), int(np.ceil(y2 - y1))
    if wi < 4 or hi < 4:
        return None
    return img[yi:yi + hi, xi:xi + wi]


def match(dets, plates, thr):
    """Greedy by score. Returns (tp_pairs {gt_idx: det}, n_fp)."""
    used, pairs, fp = set(), {}, 0
    for d in dets:
        best, bi = 0.0, -1
        for k, p in enumerate(plates):
            if p["status"] == "ok" and k not in used:
                v = iou(d[:4], p["box"])
                if v > best:
                    best, bi = v, k
        if best >= thr:
            used.add(bi)
            pairs[bi] = d
        elif not any(p["status"] == "ignore" and iou(d[:4], p["box"]) >= 0.3 for p in plates):
            fp += 1  # includes duplicates and badly localised boxes on real plates
    return pairs, fp


def ocr_scores(preds, gts):
    n = len(gts)
    raw = sum(p == g for p, g in zip(preds, gts)) / n
    norm = sum(kenya_normalise(p) == g for p, g in zip(preds, gts)) / n
    c = statistics.mean(cer(p, g) for p, g in zip(preds, gts))
    cn = statistics.mean(cer(kenya_normalise(p), g) for p, g in zip(preds, gts))
    return {"exact_raw": raw, "exact_norm": norm, "cer_raw": c, "cer_norm": cn, "n": n}


def timeit(fn, inputs, warm=10):
    for x in inputs[:warm]:
        fn(x)
    ts = []
    for x in inputs:
        t = time.perf_counter()
        fn(x)
        ts.append((time.perf_counter() - t) * 1000)
    return statistics.median(ts)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--cpu-latency-n", type=int, default=30)
    args = ap.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)
    rows = load_testset()
    gts_all = [(r, k, p) for r in rows for k, p in enumerate(r["plates"]) if p["status"] == "ok"]
    print(f"{len(rows)} images, {len(gts_all)} plates")

    det_ctor = ac.detectors()
    det_ctor["rtdetrv2-r18/letterbox"] = lambda: ac.RtDetr(
        ac.DL / "detectors/topurrra-rtdetrv2-r18/plate_rtdetr.onnx", letterbox=True)
    for k in [k for k in CONF if "@" in k]:
        det_ctor[k] = det_ctor[k.split("@")[0]]
    rec_ctor = ac.recognizers()
    res = {"detectors": {}, "oracle": {}, "e2e": {}, "latency": {}}

    # ---- detection
    det_out = {}
    for name, ctor in det_ctor.items():
        d = ctor()
        d.conf = CONF[name]
        outs = [d(r["img"]) for r in rows]
        det_out[name] = outs
        tp50 = fp50 = found30 = 0
        hit_by_src = {"seeded": [0, 0], "manual": [0, 0]}
        for r, o in zip(rows, outs):
            pairs, fp = match(o, r["plates"], 0.5)
            tp50 += len(pairs)
            fp50 += fp
            found30 += len(match(o, r["plates"], 0.3)[0])
            for k, p in enumerate(r["plates"]):
                if p["status"] == "ok":
                    src = "manual" if "manual" in p["tags"] else "seeded"
                    hit_by_src[src][0] += k in pairs
                    hit_by_src[src][1] += 1
        n = len(gts_all)
        res["detectors"][name] = {
            "recall@0.5": tp50 / n, "precision@0.5": tp50 / max(1, tp50 + fp50),
            "found@0.3": found30 / n, "tp": tp50, "fp": fp50, "conf": CONF[name],
            # 'manual' = plates a human found on the full-image sweep that no
            # candidate detector proposed at conf >= 0.05 (they bound the
            # recall bias of detector-seeded ground truth).
            "recall_seeded": hit_by_src["seeded"][0] / max(1, hit_by_src["seeded"][1]),
            "recall_manual": hit_by_src["manual"][0] / max(1, hit_by_src["manual"][1]),
            "n_seeded": hit_by_src["seeded"][1], "n_manual": hit_by_src["manual"][1]}
        res["latency"][name] = {"gpu_ms": timeit(d, [r["img"] for r in rows])}
        print(name, res["detectors"][name], res["latency"][name])

    # ---- recognizers: oracle crops
    oracle_crops = [crop(r["img"], p["box"]) for r, _, p in gts_all]
    gt_text = [p["text"] for _, _, p in gts_all]
    tags = [p["tags"] for _, _, p in gts_all]
    recs = {}
    for name, ctor in rec_ctor.items():
        rec = recs[name] = ctor()
        preds = [rec(c)[0] if c is not None else "" for c in oracle_crops]
        res["oracle"][name] = ocr_scores(preds, gt_text)
        for tag, sel in (("1line", lambda t: "2line" not in t), ("2line", lambda t: "2line" in t)):
            idx = [i for i, t in enumerate(tags) if sel(t)]
            res["oracle"][name][tag] = ocr_scores([preds[i] for i in idx], [gt_text[i] for i in idx])
        res["oracle"][name]["preds"] = preds
        res["latency"][name] = {"gpu_ms": timeit(lambda c: rec(c), [c for c in oracle_crops if c is not None])}
        print("oracle", name, {k: round(v, 3) for k, v in res["oracle"][name].items() if isinstance(v, float)},
              res["latency"][name])

    # ---- end to end (detector -> 8 % crop -> recognizer), contract-mode recognizers
    for dn, outs in det_out.items():
        pairs_per_img = [match(o, r["plates"], 0.5)[0] for r, o in zip(rows, outs)]
        for rn, rec in recs.items():
            preds = []
            for r, k, p in gts_all:
                d = pairs_per_img[rows.index(r)].get(k)
                c = crop(r["img"], d[:4]) if d is not None else None
                preds.append(rec(c)[0] if c is not None else "")
            res["e2e"][f"{dn} + {rn}"] = ocr_scores(preds, gt_text)

    # ---- CPU latency (ORT CPU EP), on a subset
    import onnxruntime as ort
    ac.PROVIDERS[:] = ["CPUExecutionProvider"]
    ac_session = ac.session

    def cpu_session(path):
        so = ort.SessionOptions()
        so.log_severity_level = 3
        return ort.InferenceSession(str(path), so, providers=["CPUExecutionProvider"])
    ac.session = cpu_session
    sub = rows[: args.cpu_latency_n]
    for name, ctor in det_ctor.items():
        d = ctor()
        d.conf = CONF[name]
        res["latency"][name]["cpu_ms"] = timeit(d, [r["img"] for r in sub], warm=3)
    for name, ctor in rec_ctor.items():
        rec = ctor()
        res["latency"][name]["cpu_ms"] = timeit(lambda c: rec(c), [c for c in oracle_crops[:60] if c is not None], warm=3)
    ac.session = ac_session
    print(json.dumps(res["latency"], indent=1))

    (OUT / "results.json").write_text(json.dumps(res, indent=1))
    return 0


if __name__ == "__main__":
    sys.exit(main())
