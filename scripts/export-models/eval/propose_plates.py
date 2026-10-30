#!/usr/bin/env python3
"""Annotation helper: union of all candidate detectors' boxes on the test-set
candidates, written as numbered review sheets so a human can confirm each box,
read its text, and add any plate every detector missed.

Bias note: GT boxes seeded from detector proposals favour plates that at least
one detector finds; the reviewer must still add missed plates by hand
(see manual boxes in manifest.csv), otherwise recall is over-estimated.

Usage:
    python3 -I scripts/export-models/eval/propose_plates.py <image_dir> <out_dir>
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

import cv2
import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
from alpr_candidates import detectors  # noqa: E402


def iou(a, b) -> float:
    ix = max(0.0, min(a[2], b[2]) - max(a[0], b[0]))
    iy = max(0.0, min(a[3], b[3]) - max(a[1], b[1]))
    inter = ix * iy
    u = (a[2] - a[0]) * (a[3] - a[1]) + (b[2] - b[0]) * (b[3] - b[1]) - inter
    return inter / u if u > 0 else 0.0


def main() -> int:
    src, out = Path(sys.argv[1]), Path(sys.argv[2])
    out.mkdir(parents=True, exist_ok=True)
    dets = {k: v() for k, v in detectors().items()}
    for d in dets.values():
        d.conf = 0.10 if not hasattr(d, "size") else 0.15
    result = {}
    for img_path in sorted(src.glob("*.jpg")):
        bgr = cv2.imread(str(img_path))
        if bgr is None:
            continue
        props: list[dict] = []
        for name, d in dets.items():
            for x1, y1, x2, y2, s in d(bgr)[:6]:
                box = [x1, y1, x2, y2]
                for p in props:
                    if iou(p["box"], box) > 0.3:
                        p["by"][name] = round(float(s), 3)
                        break
                else:
                    props.append({"box": [round(float(v), 1) for v in box], "by": {name: round(float(s), 3)}})
        result[img_path.name] = props
        if not props:
            continue
        # review sheet: crops (3x upscaled, 25 % context) stacked with their index
        tiles = []
        for i, p in enumerate(props):
            x1, y1, x2, y2 = p["box"]
            mx, my = 0.25 * (x2 - x1), 0.25 * (y2 - y1)
            c = bgr[int(max(0, y1 - my)):int(min(bgr.shape[0], y2 + my)),
                    int(max(0, x1 - mx)):int(min(bgr.shape[1], x2 + mx))]
            if c.size == 0:
                continue
            sc = 120 / c.shape[0]
            c = cv2.resize(c, (max(1, min(600, int(c.shape[1] * sc))), 120), interpolation=cv2.INTER_CUBIC)
            tile = np.full((150, 620, 3), 255, np.uint8)
            tile[25:145, 10:10 + c.shape[1]] = c
            cv2.putText(tile, f"#{i} {','.join(f'{k[:6]}:{v:.2f}' for k, v in p['by'].items())}",
                        (5, 18), cv2.FONT_HERSHEY_SIMPLEX, 0.45, (0, 0, 200), 1)
            tiles.append(tile)
        cv2.imwrite(str(out / f"{img_path.stem}__crops.jpg"), np.vstack(tiles[:8]))
    (out / "proposals.json").write_text(json.dumps(result, indent=1))
    print(f"{sum(1 for v in result.values() if v)}/{len(result)} images with proposals")
    return 0


if __name__ == "__main__":
    sys.exit(main())
