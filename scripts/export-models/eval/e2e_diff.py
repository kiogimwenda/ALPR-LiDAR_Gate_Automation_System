#!/usr/bin/env python3
"""Per-plate C++ (alpr_bench / TensorRT) vs Python (ONNX Runtime) end-to-end diff.

For every ground-truth plate where the two pipelines' reads differ, trace the
cause by re-running the Python recognizer on the C++ pipeline's own crop:

  detection    one pipeline matched the plate (IoU >= 0.5), the other did not
  crop         ORT on the C++ crop reproduces the C++ raw text, so the
               difference comes from the detector box (and hence the crop)
  recognizer   ORT on the C++ crop disagrees with the C++ raw text: a TensorRT
               recognizer numeric difference (FP16), shown as a character diff
  normaliser   C++ normalised text != kenya_normalise(C++ raw) — must be 0

Usage:
    python3 -I scripts/export-models/eval/e2e_diff.py --e2e models/downloads/eval/cpp_e2e.jsonl
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import alpr_candidates as ac  # noqa: E402
from eval_candidates import crop, kenya_normalise, load_testset, match  # noqa: E402
from propose_plates import iou  # noqa: E402


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--e2e", type=Path, required=True)
    ap.add_argument("--out", type=Path, default=ac.DL / "eval" / "e2e_diff.json")
    a = ap.parse_args()
    cpp = {d["id"]: d for d in map(json.loads, a.e2e.read_text().splitlines())}
    det = ac.detectors()["oim-yolov9-s-608"]()
    det.conf = 0.15
    rec = ac.recognizers()["fpo-cct-s-v2-global/native"]()

    rows, counts = [], {"same": 0, "detection": 0, "crop": 0, "recognizer": 0, "normaliser": 0}
    for r in load_testset():
        py_pairs, _ = match(det(r["img"]), r["plates"], 0.5)
        reads = cpp[r["id"]]["readings"]
        c_pairs, _ = match([(*x["box"], x["det_score"], x) for x in reads], r["plates"], 0.5)
        for k, p in enumerate(r["plates"]):
            if p["status"] != "ok":
                continue
            pd, cd = py_pairs.get(k), c_pairs.get(k)
            py_raw = rec(c)[0] if pd is not None and (c := crop(r["img"], pd[:4])) is not None else ""
            c_raw = cd[5]["raw"] if cd else ""
            c_norm = cd[5]["text"] if cd else ""
            if cd and kenya_normalise(c_raw) != c_norm:
                counts["normaliser"] += 1
            if py_raw == c_raw and (pd is None) == (cd is None):
                counts["same"] += 1
                continue
            entry = {"id": r["id"], "gt": p["text"], "py_raw": py_raw, "cpp_raw": c_raw,
                     "py_ok": kenya_normalise(py_raw) == p["text"], "cpp_ok": c_norm == p["text"]}
            if (pd is None) != (cd is None):
                cause = "detection"
                entry["detail"] = "matched only by " + ("C++" if cd else "Python") + " (score near floor)"
                entry["score"] = round(cd[4] if cd else pd[4], 4)
            else:
                ort_on_cpp_crop = rec(crop(r["img"], cd[:4]))[0]
                entry["box_iou_py_vs_cpp"] = round(iou(pd[:4], cd[:4]), 4)
                entry["ort_on_cpp_crop"] = ort_on_cpp_crop
                if ort_on_cpp_crop == c_raw:
                    cause = "crop"
                    entry["detail"] = "ORT on the C++ crop reproduces the C++ read"
                else:
                    cause = "recognizer"
                    entry["detail"] = "TRT recognizer vs ORT on the same crop: " + \
                        "".join(f"{x}->{y} " for x, y in zip(ort_on_cpp_crop, c_raw) if x != y)
            entry["cause"] = cause
            counts[cause] += 1
            rows.append(entry)
    a.out.write_text(json.dumps({"counts": counts, "plates": rows}, indent=1, default=float))
    print(counts)
    print(f"{'id':6s} {'GT':9s} {'Python':10s} {'C++':10s} {'py':3s} {'c++':3s} cause       detail")
    for e in rows:
        print(f"{e['id']:6s} {e['gt']:9s} {e['py_raw'] or '-':10s} {e['cpp_raw'] or '-':10s} "
              f"{'Y' if e['py_ok'] else 'n':3s} {'Y' if e['cpp_ok'] else 'n':3s} {e['cause']:11s} "
              f"{e.get('detail', '')}"
              + (f" (box IoU {e['box_iou_py_vs_cpp']})" if "box_iou_py_vs_cpp" in e else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())
