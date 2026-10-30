#!/usr/bin/env python3
"""Turn the hand-written plate annotations into the Kenya test-set manifest.

Inputs (all under models/downloads/kenya-testset/, git-ignored; committed
copies of the three text files live in scripts/export-models/eval/testset/):

  gt_notes.txt             Phase-1 review of detector proposals:
                             <img>#<proposal> <TEXT|-> <ok|ignore|no> [tags]
  review/sweep_notes.txt   Phase-2 recall sweep part 1: every proposal any
                           candidate detector made at conf >= 0.05 that Phase 1
                           had not reviewed (same format)
  review/manual_boxes.txt  Phase-2 recall sweep part 2: plates found by eye on
                           a full-image pass over every test image that NO
                           detector proposed at conf >= 0.05:
                             <img> x1 y1 x2 y2 <TEXT|-> <ok|ignore> [tags]

   ok      legible plate; TEXT was read by a human from the image
   ignore  partial / occluded / too blurry or small to read: a detection
           there is neither a TP nor an FP
   no      not a plate
<img> is the position in sorted(review/proposals.json) (propose_plates.py).

GT box for a reviewed proposal = per-coordinate median of every candidate
detector's box overlapping it (IoU > 0.3); manual boxes are used as drawn.

Output: manifest.csv (+ images/) next to the inputs, and a copy of the
manifest and the three annotation files in scripts/export-models/eval/testset/.
"""

from __future__ import annotations

import csv
import hashlib
import json
import re
import shutil
import urllib.parse
import sys
from pathlib import Path

import cv2
import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
from alpr_candidates import detectors  # noqa: E402
from propose_plates import iou  # noqa: E402

REPO = Path(__file__).resolve().parents[3]
TS = REPO / "models" / "downloads" / "kenya-testset"
TRACKED = Path(__file__).resolve().parent / "testset"

# Images where a person's face is the prominent subject (privacy rule), and
# near-duplicate frames of the same vehicle (KCE 203C appears in six frames).
EXCLUDE_FACES = {66, 80, 82, 84, 85, 137, 158, 440}
EXCLUDE_DUPES = {24, 29, 32}


def read_notes(path: Path) -> dict[int, list[tuple[int, str, str, list[str]]]]:
    out: dict[int, list[tuple[int, str, str, list[str]]]] = {}
    if not path.exists():
        return out
    for line in path.read_text().splitlines():
        if not line.strip() or line.startswith("#"):
            continue
        key, text, status, *tags = line.split()
        i, j = map(int, key.split("#"))
        if status not in ("ok", "ignore", "no"):
            status = "ignore"
        out.setdefault(i, []).append((j, text, status, tags))
    return out


def read_manual(path: Path) -> dict[int, list[dict]]:
    out: dict[int, list[dict]] = {}
    if not path.exists():
        return out
    for line in path.read_text().splitlines():
        if not line.strip() or line.startswith("#"):
            continue
        i, x1, y1, x2, y2, text, status, *tags = line.split()
        out.setdefault(int(i), []).append({
            "text": text if status == "ok" else "", "status": status,
            "box": [float(x1), float(y1), float(x2), float(y2)], "tags": tags})
    return out


def clean_author(author: str) -> str:
    """Commons 'Artist' text minus upload-tool boilerplate, whitespace collapsed."""
    a = re.sub(r"\s+", " ", author.replace("\xa0", " "))
    a = re.sub(r"\s*This file was uploaded with Commonist\.?", "", a, flags=re.I)
    return a.strip()


def clean_url(url: str) -> str:
    """Drop the utm_* tracking parameters Commons appends to image URLs."""
    parts = urllib.parse.urlsplit(url)
    query = [(k, v) for k, v in urllib.parse.parse_qsl(parts.query) if not k.startswith("utm_")]
    return urllib.parse.urlunsplit(parts._replace(query=urllib.parse.urlencode(query)))


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> int:
    props = json.loads((TS / "review" / "proposals.json").read_text())
    names = sorted(props)
    notes = read_notes(TS / "gt_notes.txt")
    for i, v in read_notes(TS / "review" / "sweep_notes.txt").items():
        notes.setdefault(i, []).extend(v)
    manual = read_manual(TS / "review" / "manual_boxes.txt")

    dets = {k: v() for k, v in detectors().items()}
    for d in dets.values():
        d.conf = 0.05
    (TS / "images").mkdir(exist_ok=True)
    rows = []
    for i in sorted(set(notes) | set(manual)):
        if i in EXCLUDE_FACES or i in EXCLUDE_DUPES:
            continue
        if not any(s == "ok" for _, _, s, _ in notes.get(i, [])) and \
                not any(p["status"] == "ok" for p in manual.get(i, [])):
            continue
        name = names[i]
        meta = json.loads((TS / "candidates" / name.replace(".jpg", ".json")).read_text())
        bgr = cv2.imread(str(TS / "candidates" / name))
        all_boxes = [b[:4] for d in dets.values() for b in d(bgr)]
        plates = []
        for j, text, status, tags in notes.get(i, []):
            if status == "no":
                continue
            seed = props[name][j]["box"]
            near = [b for b in all_boxes if iou(b, seed) > 0.3] or [seed]
            box = [round(float(v), 1) for v in np.median(np.array(near), axis=0)]
            plates.append({"text": text if status == "ok" else "", "status": status,
                           "box": box, "tags": tags})
        plates.extend(manual.get(i, []))
        shutil.copy2(TS / "candidates" / name, TS / "images" / name)
        rows.append({
            "id": f"ke{len(rows):03d}", "file": f"images/{name}",
            "sha256": sha256(TS / "candidates" / name),
            "source_page": meta["page"], "image_url": clean_url(meta["image_url"]),
            "author": clean_author(meta["author"]), "licence": meta["licence"], "licence_url": meta["licence_url"],
            "commons_category": meta["category"], "width": bgr.shape[1], "height": bgr.shape[0],
            "n_legible": sum(p["status"] == "ok" for p in plates),
            "plates_json": json.dumps(plates),
        })
    with (TS / "manifest.csv").open("w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0]))
        w.writeheader()
        w.writerows(rows)
    TRACKED.mkdir(exist_ok=True)
    shutil.copy2(TS / "manifest.csv", TRACKED / "manifest.csv")
    shutil.copy2(TS / "gt_notes.txt", TRACKED / "gt_notes.txt")
    for f in ("sweep_notes.txt", "manual_boxes.txt"):
        if (TS / "review" / f).exists():
            shutil.copy2(TS / "review" / f, TRACKED / f)
    n_ok = sum(r["n_legible"] for r in rows)
    print(f"{len(rows)} images, {n_ok} legible plates")
    return 0


if __name__ == "__main__":
    sys.exit(main())
