#!/usr/bin/env python3
"""Harvest candidate Kenyan-vehicle photos from Wikimedia Commons for the ALPR test set.

Walks a fixed set of Commons categories (to a small depth), keeps files with a
free licence (CC0 / PD / CC BY / CC BY-SA), and downloads a 1600 px-wide
rendition plus a JSON record (source page, author, licence) for each one into
models/downloads/kenya-testset/candidates/. A human then reviews the
candidates, keeps the ones with a legible plate and writes the ground truth
into models/downloads/kenya-testset/manifest.csv.

Usage:
    python3 -I scripts/export-models/eval/collect_commons_candidates.py [--max 400]
"""

from __future__ import annotations

import argparse
import json
import re
import sys
import time
import urllib.parse
import urllib.request
from pathlib import Path

API = "https://commons.wikimedia.org/w/api.php"
UA = "gate-automation-alpr-testset/1.0 (research; contact via repo owner)"
OUT = Path(__file__).resolve().parents[3] / "models" / "downloads" / "kenya-testset" / "candidates"

ROOTS = [
    "License plates of Kenya",
    "Matatus",
    "Buses in Nairobi",
    "Buses in Kenya",
    "Motorcycle taxis in Kenya",
    "Boda-boda in Kenya",
    "Motorcycles in Kenya",
    "Tuk-tuks in Kenya",
    "Trucks in Kenya",
    "Pickup trucks in Kenya",
    "Automobiles in Kenya",
    "Parked automobiles in Kenya",
    "Safari vehicles in Kenya",
    "Military trucks of Kenya",
]
FREE = re.compile(r"^(cc0|pd|public domain|cc by(-sa)? ?[1-4]\.0|cc by(-sa)?)", re.I)


def api(**params) -> dict:
    params |= {"format": "json"}
    req = urllib.request.Request(API + "?" + urllib.parse.urlencode(params), headers={"User-Agent": UA})
    err: Exception | None = None
    for attempt in range(8):
        try:
            time.sleep(0.5)  # be polite: Commons rate-limits bursty clients
            with urllib.request.urlopen(req, timeout=60) as r:
                return json.load(r)
        except Exception as exc:  # noqa: BLE001 — transient network / 429
            err = exc
            time.sleep(5 * (attempt + 1))
    raise RuntimeError(f"Commons API failed: {params}: {err!r}")


def walk(cat: str, depth: int, seen: set[str], files: dict[str, str]) -> None:
    if cat in seen:
        return
    seen.add(cat)
    cont: dict = {}
    while True:
        d = api(action="query", list="categorymembers", cmtitle=f"Category:{cat}",
                cmlimit=500, cmtype="file|subcat", **cont)
        for m in d.get("query", {}).get("categorymembers", []):
            t = m["title"]
            if t.startswith("Category:") and depth > 0:
                walk(t.removeprefix("Category:"), depth - 1, seen, files)
            elif t.startswith("File:") and t.lower().endswith((".jpg", ".jpeg", ".png")):
                files.setdefault(t, cat)
        if "continue" not in d:
            break
        cont = d["continue"]


def strip_html(s: str) -> str:
    return re.sub(r"<[^>]+>", "", s or "").strip()


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--max", type=int, default=400)
    ap.add_argument("--depth", type=int, default=1)
    args = ap.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)

    files: dict[str, str] = {}
    seen: set[str] = set()
    for root in ROOTS:
        walk(root, args.depth, seen, files)
    print(f"{len(files)} files across {len(seen)} categories")

    titles = sorted(files)[: args.max * 3]
    kept = 0
    for i in range(0, len(titles), 40):
        batch = titles[i:i + 40]
        d = api(action="query", titles="|".join(batch), prop="imageinfo",
                iiprop="url|extmetadata|size", iiurlwidth=1600)
        for page in d.get("query", {}).get("pages", {}).values():
            ii = (page.get("imageinfo") or [{}])[0]
            meta = ii.get("extmetadata", {})
            lic = strip_html(meta.get("LicenseShortName", {}).get("value", ""))
            if not FREE.match(lic):
                continue
            stem = re.sub(r"[^A-Za-z0-9._-]+", "_", page["title"].removeprefix("File:"))[:100]
            stem = Path(stem).stem
            img = OUT / f"{stem}.jpg"
            rec = {
                "title": page["title"],
                "category": files.get(page["title"]),
                "page": ii.get("descriptionurl"),
                "image_url": ii.get("thumburl") or ii.get("url"),
                "licence": lic,
                "licence_url": strip_html(meta.get("LicenseUrl", {}).get("value", "")),
                "author": strip_html(meta.get("Artist", {}).get("value", "")),
                "width": ii.get("width"), "height": ii.get("height"),
            }
            if not img.exists():
                req = urllib.request.Request(rec["image_url"], headers={"User-Agent": UA})
                try:
                    with urllib.request.urlopen(req, timeout=120) as r:
                        img.write_bytes(r.read())
                except Exception as exc:  # noqa: BLE001
                    print(f"skip {page['title']}: {exc}")
                    continue
                time.sleep(0.3)
            (OUT / f"{stem}.json").write_text(json.dumps(rec, indent=1, ensure_ascii=False))
            kept += 1
            if kept >= args.max:
                print(f"kept {kept}")
                return 0
    print(f"kept {kept}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
