#!/usr/bin/env python3
"""Draw a detector + recognizer pair's output over every test-set image.

Green = GT plate (hand-read text above), orange = ignore region,
blue = detection with its recognized text (contract-mode recognizer).

Usage:
    python3 -I scripts/export-models/eval/render_eval.py <detector> <recognizer> [out_subdir]
    e.g. render_eval.py oim-yolov9-t-640 fpo-cct-s-v2-global/contract
"""

from __future__ import annotations

import sys
from pathlib import Path

import cv2

sys.path.insert(0, str(Path(__file__).resolve().parent))
import alpr_candidates as ac  # noqa: E402
from eval_candidates import CONF, OUT, crop, kenya_normalise, load_testset  # noqa: E402


def main() -> int:
    dn, rn = sys.argv[1], sys.argv[2]
    sub = sys.argv[3] if len(sys.argv) > 3 else "annotated"
    out = OUT / sub
    out.mkdir(parents=True, exist_ok=True)
    det = ac.detectors()[dn.split("@")[0]]()
    det.conf = CONF[dn]
    rec = ac.recognizers()[rn]()
    for r in load_testset():
        img = r["img"].copy()
        sc = max(1.0, img.shape[1] / 1600)
        th = max(2, int(2 * sc))
        for p in r["plates"]:
            x1, y1, x2, y2 = map(int, p["box"])
            col = (0, 200, 0) if p["status"] == "ok" else (0, 165, 255)
            cv2.rectangle(img, (x1, y1), (x2, y2), col, th)
            if p["text"]:
                cv2.putText(img, f"GT {p['text']}", (x1, y2 + int(28 * sc)), cv2.FONT_HERSHEY_SIMPLEX,
                            0.8 * sc, col, th)
        # Labels are stacked: each one is placed above its box and pushed further
        # up until it no longer overlaps a label already drawn (adjacent plates).
        placed: list[tuple[int, int, int, int]] = []
        font, fs = cv2.FONT_HERSHEY_SIMPLEX, 0.8 * sc
        for x1, y1, x2, y2, s in det(r["img"]):
            c = crop(r["img"], (x1, y1, x2, y2))
            text, conf = rec(c) if c is not None else ("", 0.0)
            norm = kenya_normalise(text)
            label = f"{norm} {s:.2f}/{conf:.2f}" + ("" if norm == text else f" (raw {text})")
            cv2.rectangle(img, (int(x1), int(y1)), (int(x2), int(y2)), (255, 80, 0), th)
            (tw, tht), base = cv2.getTextSize(label, font, fs, th)
            lx, ly = int(x1), int(y1) - 8
            def hits(a):
                return any(a[0] < b[2] and b[0] < a[2] and a[1] < b[3] and b[1] < a[3] for b in placed)
            while hits((lx, ly - tht, lx + tw, ly + base)) and ly - tht > 0:
                ly -= tht + base + 4
            ly = max(tht + 2, ly)
            placed.append((lx, ly - tht, lx + tw, ly + base))
            cv2.rectangle(img, (lx - 2, ly - tht - 2), (lx + tw + 2, ly + base), (255, 255, 255), -1)
            cv2.putText(img, label, (lx, ly), font, fs, (255, 80, 0), th)
        cv2.imwrite(str(out / f"{r['id']}.jpg"), img, [cv2.IMWRITE_JPEG_QUALITY, 85])
    print(f"wrote {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
