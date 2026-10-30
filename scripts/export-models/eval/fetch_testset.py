#!/usr/bin/env python3
"""Re-download the Kenya ALPR test set from Wikimedia Commons and verify it.

Reads scripts/export-models/eval/testset/manifest.csv (committed: source
page, author, licence, SHA-256 and hand-read ground truth per image), fetches
each image_url into models/downloads/kenya-testset/images/ (git-ignored) and
checks its SHA-256. A mismatching download is deleted and reported; the
script exits non-zero if any image is missing or wrong.

The images stay under their own licences (CC BY / CC BY-SA / CC0, see the
manifest's author / licence / source_page columns); they are fetched for
evaluation and never committed.

Usage:
    python3 -I scripts/export-models/eval/fetch_testset.py [--force]
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import sys
import time
import urllib.request
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
MANIFEST = HERE / "testset" / "manifest.csv"
OUT = REPO / "models" / "downloads" / "kenya-testset"
UA = "gate-automation-alpr-testset/1.0 (evaluation; see repo)"


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def fetch(url: str, dest: Path) -> None:
    req = urllib.request.Request(url, headers={"User-Agent": UA})
    for attempt in range(6):
        try:
            with urllib.request.urlopen(req, timeout=120) as r:
                dest.write_bytes(r.read())
            return
        except Exception:  # noqa: BLE001 — Commons rate-limits (HTTP 429)
            if attempt == 5:
                raise
            time.sleep(5 * (attempt + 1))


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--force", action="store_true", help="re-download even if present")
    args = ap.parse_args()
    rows = list(csv.DictReader(MANIFEST.open()))
    bad = 0
    for r in rows:
        dest = OUT / r["file"]
        dest.parent.mkdir(parents=True, exist_ok=True)
        if args.force or not dest.exists():
            try:
                fetch(r["image_url"], dest)
                time.sleep(0.5)
            except Exception as exc:  # noqa: BLE001
                print(f"FAILED  {r['id']} {r['image_url']}: {exc}")
                bad += 1
                continue
        got = sha256(dest)
        if got != r["sha256"]:
            print(f"MISMATCH {r['id']} {dest.name}: {got} != {r['sha256']} — deleting")
            dest.unlink()
            bad += 1
    print(f"{len(rows) - bad}/{len(rows)} images verified in {OUT / 'images'}")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
