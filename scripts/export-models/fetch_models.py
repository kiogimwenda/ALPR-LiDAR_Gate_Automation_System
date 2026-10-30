#!/usr/bin/env python3
"""Fetch the Phase-1 ALPR candidate models into models/downloads/ (git-ignored).

Every artifact is pinned by URL (immutable GitHub release asset or a Hugging
Face commit SHA) and verified against a SHA-256 checksum. A file whose hash
does not match is deleted and the script exits non-zero.

Usage:
    python3 -I scripts/export-models/fetch_models.py            # fetch + verify all
    python3 -I scripts/export-models/fetch_models.py --only det  # detectors only
    python3 -I scripts/export-models/fetch_models.py --only prod # the deployed pair only
    python3 -I scripts/export-models/fetch_models.py --print-hashes  # (re)compute, for pinning

Licences (see models/downloads/eval/RESULTS.md for the full discussion):
    open-image-models YOLOv9 plate detectors  repo MIT; weights trained with a fork of
                                              WongKinYiu/yolov9 (GPL-3.0) -> treat as GPL-3.0
    morsetechlab YOLOv11 plate detector       AGPL-3.0 (Ultralytics)
    Topurrra RT-DETRv2-r18 plate detector     Apache-2.0
    fast-plate-ocr CCT global recognizers     MIT (maintainer: weights MIT, no restrictions)
    PaddleOCR PP-OCRv4/v5 rec                 Apache-2.0
"""

from __future__ import annotations

import argparse
import hashlib
import sys
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2] / "models" / "downloads"

OIM = "https://github.com/ankandrew/open-image-models/releases/download/assets"
FPO = "https://github.com/ankandrew/fast-plate-ocr/releases/download/arg-plates"
HF = "https://huggingface.co"

# (group, relative destination, url)
ARTIFACTS: list[tuple[str, str, str]] = [
    # ---- detectors -------------------------------------------------------
    ("det", "detectors/oim-yolov9-t-640/yolo-v9-t-640-license-plates-end2end.onnx",
     f"{OIM}/yolo-v9-t-640-license-plates-end2end.onnx"),
    ("det", "detectors/oim-yolov9-t-640/yolo-v9-t-640-license-plates.pt",
     f"{OIM}/yolo-v9-t-640-license-plates.pt"),
    ("det", "detectors/oim-yolov9-s-608/yolo-v9-s-608-license-plates-end2end.onnx",
     f"{OIM}/yolo-v9-s-608-license-plates-end2end.onnx"),
    ("det", "detectors/oim-yolov9-s-608/yolo-v9-s-608-license-plates.pt",
     f"{OIM}/yolo-v9-s-608-license-plates.pt"),
    ("det", "detectors/morsetech-yolov11s/license-plate-finetune-v1s.pt",
     f"{HF}/morsetechlab/yolov11-license-plate-detection/resolve/@REV_MORSE@/license-plate-finetune-v1s.pt"),
    ("det", "detectors/topurrra-rtdetrv2-r18/plate_rtdetr.onnx",
     f"{HF}/Topurrra/rtdetr-license-plate-detection-onnx/resolve/@REV_TOP@/plate_rtdetr.onnx"),
    ("det", "detectors/topurrra-rtdetrv2-r18/preprocessor_config.json",
     f"{HF}/Topurrra/rtdetr-license-plate-detection-onnx/resolve/@REV_TOP@/preprocessor_config.json"),
    # ---- recognizers -----------------------------------------------------
    ("rec", "recognizers/fpo-cct-s-v2-global/cct_s_v2_global.onnx",
     f"{FPO}/cct_s_v2_global.onnx"),
    ("rec", "recognizers/fpo-cct-s-v2-global/cct_s_v2_global_plate_config.yaml",
     f"{FPO}/cct_s_v2_global_plate_config.yaml"),
    ("rec", "recognizers/fpo-cct-xs-v2-global/cct_xs_v2_global.onnx",
     f"{FPO}/cct_xs_v2_global.onnx"),
    ("rec", "recognizers/fpo-cct-xs-v2-global/cct_xs_v2_global_plate_config.yaml",
     f"{FPO}/cct_xs_v2_global_plate_config.yaml"),
]

# PaddleOCR rec checkpoints (PaddlePaddle 3 PIR format), pinned HF commits.
PADDLE = {
    "en_PP-OCRv4_mobile_rec": "f97b62fdc0eb71c689393a19a4b21baa1795c9ab",
    "en_PP-OCRv5_mobile_rec": "267c36e24c331595590fe7bd72bde2436fd286f2",
    "latin_PP-OCRv5_mobile_rec": "ab2cd5cc5fa6309be2e5acdfe66eca2c2c127d57",
}
for _name, _rev in PADDLE.items():
    for _f in ("inference.json", "inference.pdiparams", "inference.yml", "config.json"):
        ARTIFACTS.append(("rec", f"recognizers/{_name}/{_f}",
                          f"{HF}/PaddlePaddle/{_name}/resolve/{_rev}/{_f}"))

# The deployed pair (see models/downloads/eval/RESULTS.md and
# docs/decisions/ADR-014-alpr-model-selection.md).
PROD = {
    "detectors/oim-yolov9-s-608/yolo-v9-s-608-license-plates-end2end.onnx",
    "recognizers/fpo-cct-s-v2-global/cct_s_v2_global.onnx",
    "recognizers/fpo-cct-s-v2-global/cct_s_v2_global_plate_config.yaml",
}

REVISIONS = {
    "@REV_MORSE@": "251a30d7daedca065f56e04b0af04052c907c68f",
    "@REV_TOP@": "aecb24a78e31cea03f59b83156d55dde84f20d94",
}

# Pinned checksums (generated with --print-hashes, then pasted here).
SHA256: dict[str, str] = {
    'detectors/oim-yolov9-t-640/yolo-v9-t-640-license-plates-end2end.onnx': 'c3c1026ca7d0585dd88084d68182dd897113712fa734ae1557ca70174440c076',
    'detectors/oim-yolov9-t-640/yolo-v9-t-640-license-plates.pt': '3b4702e22a0785dd4bd71ddb6892a1572609955a2b4bfef4a52377a4ece58fc8',
    'detectors/oim-yolov9-s-608/yolo-v9-s-608-license-plates-end2end.onnx': '2b878b38d9aa07b6ddc3ea75c4ffcb39869bc5c218e0a14002f60ab2f7b0be9a',
    'detectors/oim-yolov9-s-608/yolo-v9-s-608-license-plates.pt': 'b0c0d2309dedc9447b7c55e730f9c917d99e9690d71d1c383e419479624d60b6',
    'detectors/morsetech-yolov11s/license-plate-finetune-v1s.pt': '95e50c25ab7066dd0ca5aec18fa80349676db08697780d1149576461174d2381',
    'detectors/topurrra-rtdetrv2-r18/plate_rtdetr.onnx': '50f9bf9d7eaa97ade59063ba608107203d0575a84c0fb27e0ebf3bab6f594366',
    'detectors/topurrra-rtdetrv2-r18/preprocessor_config.json': '7e9e2245806aa3ef068a5321f5eebee3116fa4cec560173231954ffaca930eca',
    'recognizers/fpo-cct-s-v2-global/cct_s_v2_global.onnx': '384bbbd2cea3ef54761d3df70822ef3a349ee1a112aeafddbe0e3ba06bc6e47b',
    'recognizers/fpo-cct-s-v2-global/cct_s_v2_global_plate_config.yaml': '0335c74a305173bb6f393efed0fde03cadeaa0b649ed8e19f431016d8232d0a6',
    'recognizers/fpo-cct-xs-v2-global/cct_xs_v2_global.onnx': '8031afb5fdc6b4d80462c9d542f1284ebd2cfddf5dbacd62609848d7e2855f44',
    'recognizers/fpo-cct-xs-v2-global/cct_xs_v2_global_plate_config.yaml': '0335c74a305173bb6f393efed0fde03cadeaa0b649ed8e19f431016d8232d0a6',
    'recognizers/en_PP-OCRv4_mobile_rec/inference.json': '7a2302e09f0d501458db082cbcf21cdcb3a8d6dc9a143f73b17e297505f922f9',
    'recognizers/en_PP-OCRv4_mobile_rec/inference.pdiparams': '75f64a1ffb70c56b7a25655963ca16f5bf3286202e3f52ac972bee05cdee2f56',
    'recognizers/en_PP-OCRv4_mobile_rec/inference.yml': '5f6b19b6b09b6b6f7924de6c25076d0b3528b6f641e64d4f12e6e6207ec33200',
    'recognizers/en_PP-OCRv4_mobile_rec/config.json': 'c1ebffdece1c8eb30515cc3df9bed55c8806fbc57e4231ec7be83f43c57a3660',
    'recognizers/en_PP-OCRv5_mobile_rec/inference.json': 'fd1b6ec722ea841a72d3ba43e527df1d1066d5d7808e0503ee3eec7265188753',
    'recognizers/en_PP-OCRv5_mobile_rec/inference.pdiparams': '3ec8a97ed6cefe8568d3e2ee90bb193299b566a7661aa4fd52d224b96b59f66b',
    'recognizers/en_PP-OCRv5_mobile_rec/inference.yml': '27e91d0582f40168aa218303c76e184bc78fa7a5d105aad0cfbad8458b441067',
    'recognizers/en_PP-OCRv5_mobile_rec/config.json': '16103ac291a82fca50f0bb000d62a4b8168f6042f03c89c44cf32f241d1eb1f2',
    'recognizers/latin_PP-OCRv5_mobile_rec/inference.json': '820b2ee8e5072de5156c3d55124c2b03c82ef278d59f453f857c0789bacb4da1',
    'recognizers/latin_PP-OCRv5_mobile_rec/inference.pdiparams': '53cdc8b481a7394bb108f96d0fb3432b0a8f392e22c7d18f06dbb2d42b8b25f9',
    'recognizers/latin_PP-OCRv5_mobile_rec/inference.yml': '0bbe984570f597af3638e50bdf2e8276f3ab26a61966096538b3b0d1849f5c84',
    'recognizers/latin_PP-OCRv5_mobile_rec/config.json': 'e0f5ccc44fe86254956f27746058bc4cf88d86145668cecf22ba5dd962483c80',
}


def sha256_of(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def resolve(url: str) -> str:
    for k, v in REVISIONS.items():
        url = url.replace(k, v)
    return url


def download(url: str, dest: Path) -> None:
    dest.parent.mkdir(parents=True, exist_ok=True)
    tmp = dest.with_suffix(dest.suffix + ".part")
    req = urllib.request.Request(url, headers={"User-Agent": "gate-automation-fetch/1"})
    with urllib.request.urlopen(req, timeout=120) as r, tmp.open("wb") as f:
        while chunk := r.read(1 << 20):
            f.write(chunk)
    tmp.replace(dest)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--only", choices=("det", "rec", "prod"),
                    help="prod = just the two deployed models (prepare_onnx.sh)")
    ap.add_argument("--print-hashes", action="store_true")
    ap.add_argument("--force", action="store_true", help="re-download even if present")
    args = ap.parse_args()

    bad = 0
    for group, rel, url in ARTIFACTS:
        if args.only == "prod":
            if rel not in PROD:
                continue
        elif args.only and group != args.only:
            continue
        dest = ROOT / rel
        if args.force or not dest.exists():
            print(f"fetch  {rel}")
            download(resolve(url), dest)
        digest = sha256_of(dest)
        if args.print_hashes:
            print(f"    {rel!r}: {digest!r},")
            continue
        want = SHA256.get(rel)
        if want is None:
            print(f"UNPINNED {rel} sha256={digest}")
            bad += 1
        elif want != digest:
            print(f"MISMATCH {rel}: got {digest}, want {want} — deleting")
            dest.unlink()
            bad += 1
        else:
            print(f"ok     {rel}")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
