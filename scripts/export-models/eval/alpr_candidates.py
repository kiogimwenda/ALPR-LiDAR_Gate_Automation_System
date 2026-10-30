"""Runners for the Phase-1 ALPR candidate models (ONNX Runtime, CUDA EP).

Every runner exposes the same small interface so eval_candidates.py can mix
and match them:

    Detector:   __call__(bgr) -> list[(x1, y1, x2, y2, score)]  original-image pixels
    Recognizer: __call__(crop_bgr) -> (text, confidence)

Recognizers come in two flavours:
  * "native"   — the preprocessing / decoding the upstream project uses;
  * "contract" — exactly what the pre-Phase-2 server/inference/src/paddle_ocr_recognizer.cpp
                 does today (aspect-preserving resize to 48 px high, right-pad
                 with BLACK to 320, BGR, (x/255-0.5)/0.5, NCHW) followed by a
                 37-class [blank + dict_kenya_plates.txt] output and the C++
                 greedy CTC decode. This tells us what the C++ server would
                 read if the model were dropped in through an ONNX adapter.
"""

from __future__ import annotations

import math
from pathlib import Path

import cv2
import numpy as np
import onnxruntime as ort
import yaml

REPO = Path(__file__).resolve().parents[3]
DL = REPO / "models" / "downloads"
KENYA_DICT = (REPO / "server" / "models" / "dict_kenya_plates.txt").read_text().splitlines()
assert len(KENYA_DICT) == 36

PROVIDERS = ["CUDAExecutionProvider", "CPUExecutionProvider"]
ort.set_default_logger_severity(3)
try:
    # onnxruntime-gpu 1.24 is built against CUDA 12 / cuDNN 9; the host has CUDA 13.
    # torch's cu128 wheels ship the CUDA-12 user-space libs: load those first.
    import torch  # noqa: F401
    ort.preload_dlls(cuda=True, cudnn=True, msvc=False)
except Exception:  # noqa: BLE001 — falls back to CPU EP
    pass


def session(path: Path) -> ort.InferenceSession:
    so = ort.SessionOptions()
    so.log_severity_level = 3
    s = ort.InferenceSession(str(path), so, providers=PROVIDERS)
    if s.get_providers()[0] != "CUDAExecutionProvider":
        raise RuntimeError(f"CUDA EP unavailable for {path.name}: {s.get_providers()}")
    return s


# --------------------------------------------------------------------------
# Detectors
# --------------------------------------------------------------------------

def letterbox(bgr: np.ndarray, size: int) -> tuple[np.ndarray, float, float, float]:
    """Same maths as server/inference/src/detail/cpu_algorithms.cpp::letterbox."""
    h, w = bgr.shape[:2]
    s = min(size / w, size / h)
    nw, nh = round(w * s), round(h * s)
    canvas = np.full((size, size, 3), 114, np.uint8)
    px, py = (size - nw) // 2, (size - nh) // 2
    canvas[py:py + nh, px:px + nw] = cv2.resize(bgr, (nw, nh), interpolation=cv2.INTER_LINEAR)
    return canvas, s, float(px), float(py)


def to_chw_rgb01(canvas_bgr: np.ndarray) -> np.ndarray:
    return np.ascontiguousarray(canvas_bgr[:, :, ::-1].transpose(2, 0, 1)[None], np.float32) / 255.0


class _YoloBase:
    size = 640
    name = "?"

    def __init__(self, onnx: Path, conf: float = 0.25):
        self.sess = session(onnx)
        self.inp = self.sess.get_inputs()[0].name
        self.size = self.sess.get_inputs()[0].shape[2]
        self.conf = conf

    def _raw(self, x: np.ndarray) -> np.ndarray:  # -> rows of (x1,y1,x2,y2,score) in canvas px
        raise NotImplementedError

    def __call__(self, bgr: np.ndarray):
        canvas, s, px, py = letterbox(bgr, self.size)
        rows = self._raw(to_chw_rgb01(canvas))
        h, w = bgr.shape[:2]
        out = []
        for x1, y1, x2, y2, sc in rows:
            if sc < self.conf:
                continue
            x1 = max(0.0, (x1 - px) / s); y1 = max(0.0, (y1 - py) / s)
            x2 = min(float(w), (x2 - px) / s); y2 = min(float(h), (y2 - py) / s)
            if x2 > x1 and y2 > y1:
                out.append((x1, y1, x2, y2, float(sc)))
        return sorted(out, key=lambda r: -r[4])


class OimYolov9(_YoloBase):
    """ankandrew/open-image-models end2end export: output0 [N, 7] =
    (batch, x1, y1, x2, y2, class, score), ONNX NonMaxSuppression inside."""

    def _raw(self, x):
        o = self.sess.run(None, {self.inp: x})[0]
        return [(r[1], r[2], r[3], r[4], r[6]) for r in o]


class UltralyticsNms(_YoloBase):
    """Ultralytics export with nms=True: output0 [1, 300, 6] = (x1,y1,x2,y2,score,cls)."""

    def _raw(self, x):
        o = self.sess.run(None, {self.inp: x})[0][0]
        return [(r[0], r[1], r[2], r[3], r[4]) for r in o]


class RtDetr:
    """Topurrra RT-DETRv2-r18: plain resize to 640x640 RGB/255, sigmoid(logits),
    cxcywh normalized boxes, NMS-free (we add a light NMS)."""

    def __init__(self, onnx: Path, conf: float = 0.05, letterbox: bool = False):
        self.sess = session(onnx)
        self.conf = conf
        self.lb = letterbox  # True = the C++ detector preprocessing (114-gray letterbox)

    def __call__(self, bgr):
        h, w = bgr.shape[:2]
        if self.lb:
            canvas, s, px, py = letterbox(bgr, 640)
            x = to_chw_rgb01(canvas)
        else:
            x = cv2.resize(bgr, (640, 640), interpolation=cv2.INTER_CUBIC)
            x = np.ascontiguousarray(x[:, :, ::-1].transpose(2, 0, 1)[None], np.float32) / 255.0
        logits, boxes = self.sess.run(None, {"pixel_values": x})
        sc = 1 / (1 + np.exp(-logits[0, :, 0]))
        keep = np.where(sc >= self.conf)[0]
        cand = []
        for i in keep:
            cx, cy, bw, bh = boxes[0, i]
            if self.lb:
                b = ((cx - bw / 2) * 640, (cy - bh / 2) * 640, (cx + bw / 2) * 640, (cy + bh / 2) * 640)
                b = ((b[0] - px) / s, (b[1] - py) / s, (b[2] - px) / s, (b[3] - py) / s)
            else:
                b = ((cx - bw / 2) * w, (cy - bh / 2) * h, (cx + bw / 2) * w, (cy + bh / 2) * h)
            cand.append((*b, float(sc[i])))
        if not cand:
            return []
        b = [[c[0], c[1], c[2] - c[0], c[3] - c[1]] for c in cand]
        idx = cv2.dnn.NMSBoxes(b, [c[4] for c in cand], self.conf, 0.5)
        return sorted([cand[i] for i in np.array(idx).flatten()], key=lambda r: -r[4])


def detectors() -> dict:
    d = DL / "detectors"
    return {
        "oim-yolov9-t-640": lambda: OimYolov9(d / "oim-yolov9-t-640/yolo-v9-t-640-license-plates-end2end.onnx"),
        "oim-yolov9-s-608": lambda: OimYolov9(d / "oim-yolov9-s-608/yolo-v9-s-608-license-plates-end2end.onnx"),
        "morsetech-yolov11s": lambda: UltralyticsNms(d / "morsetech-yolov11s/license-plate-finetune-v1s.onnx"),
        "rtdetrv2-r18": lambda: RtDetr(d / "topurrra-rtdetrv2-r18/plate_rtdetr.onnx"),
    }


# --------------------------------------------------------------------------
# Recognizers
# --------------------------------------------------------------------------

def cpp_contract_canvas(crop_bgr: np.ndarray, h: int = 48, w: int = 320) -> np.ndarray:
    """uint8 BGR canvas as the pre-Phase-2 PaddleOcrRecognizer::preprocess_ built it (black pad)."""
    aspect = crop_bgr.shape[1] / crop_bgr.shape[0]
    nw = int(np.clip(round(h * aspect), 1, w))
    canvas = np.zeros((h, w, 3), np.uint8)
    canvas[:, :nw] = cv2.resize(crop_bgr, (nw, h), interpolation=cv2.INTER_LINEAR)
    return canvas


def ctc_greedy(probs: np.ndarray, dictionary: list[str]) -> tuple[str, float]:
    """Mirror of detail::ctc_greedy_decode (blank = index 0)."""
    idx = probs.argmax(-1)
    best = probs.max(-1)
    out, confs, prev = [], [], -1
    for t, k in enumerate(idx):
        if k != 0 and k != prev:
            if k - 1 < len(dictionary):
                out.append(dictionary[k - 1])
            confs.append(best[t])
        prev = k
    return "".join(out), float(np.mean(confs)) if confs else 0.0


def kenya_projection(src_dict: list[str]) -> np.ndarray:
    """[C_src, 37] 0/1 matrix: blank + every character outside the Kenya set -> class 0;
    'A'/'a' -> 'A' etc. Applied to post-softmax probabilities, rows still sum to 1.
    This is the ONNX adapter we would bake in as a trailing MatMul."""
    C = len(src_dict) + 1
    P = np.zeros((C, 37), np.float32)
    P[0, 0] = 1
    pos = {c: i + 1 for i, c in enumerate(KENYA_DICT)}
    for i, ch in enumerate(src_dict):
        P[i + 1, pos.get(ch.upper(), 0)] = 1
    return P


class PaddleRec:
    def __init__(self, name: str, mode: str = "native"):
        rd = DL / "recognizers"
        self.sess = session(rd / "onnx" / f"{name}.onnx")
        y = yaml.safe_load((rd / name / "inference.yml").read_text())
        chars = list(y["PostProcess"]["character_dict"])
        self.dict = chars + [" "]  # use_space_char: True in every PP-OCR rec config
        self.mode = mode
        self.P = kenya_projection(self.dict)

    def _native_input(self, crop):
        h, w = crop.shape[:2]
        ratio = w / h
        imgW = max(320, int(math.ceil(48 * ratio)))
        rw = min(imgW, int(math.ceil(48 * ratio)))
        r = cv2.resize(crop, (rw, 48)).astype(np.float32).transpose(2, 0, 1) / 255.0
        r = (r - 0.5) / 0.5
        x = np.zeros((3, 48, imgW), np.float32)
        x[:, :, :rw] = r
        return x[None]

    def __call__(self, crop):
        if self.mode == "native":
            p = self.sess.run(None, {"x": self._native_input(crop)})[0][0]
            text, conf = ctc_greedy(p, self.dict)
            return "".join(c for c in text.upper() if c in KENYA_DICT), conf
        canvas = cpp_contract_canvas(crop).astype(np.float32) / 255.0
        if self.mode == "contract-graypad":
            # Diagnostic: C++ path but padding with 0 in *normalised* space (pixel 127.5),
            # which is what PaddleOCR's own resize_norm_img does.
            nw = int(np.clip(round(48 * crop.shape[1] / crop.shape[0]), 1, 320))
            canvas[:, nw:] = 0.5
        x = ((canvas - 0.5) / 0.5).transpose(2, 0, 1)[None].astype(np.float32)
        p = self.sess.run(None, {"x": x})[0][0] @ self.P
        return ctc_greedy(p, KENYA_DICT)


class FastPlateOcr:
    """ankandrew/fast-plate-ocr CCT v2: uint8 NHWC RGB 64x128 (stretched, no aspect
    keep); output 'plate' [N, 10 slots, 37] softmax with alphabet 0-9A-Z + '_' pad.
    Not CTC: every slot is independent, so repeated characters are NOT collapsed."""

    def __init__(self, name: str, mode: str = "native"):
        rd = DL / "recognizers" / f"fpo-{name}"
        stem = name.replace("-", "_")
        self.sess = session(rd / f"{stem}.onnx")
        cfg = yaml.safe_load((rd / f"{stem}_plate_config.yaml").read_text())
        self.alphabet, self.pad = cfg["alphabet"], cfg["pad_char"]
        self.W, self.H = cfg["img_width"], cfg["img_height"]
        self.mode = mode

    def _run(self, rgb64x128):
        out = self.sess.run(["plate"], {"input": rgb64x128[None]})[0][0]
        idx, best = out.argmax(-1), out.max(-1)
        chars = [(self.alphabet[i], b) for i, b in zip(idx, best) if self.alphabet[i] != self.pad]
        text = "".join(c for c, _ in chars)
        return text, float(np.mean([b for _, b in chars])) if chars else 0.0

    def __call__(self, crop):
        if self.mode == "native":
            img = cv2.resize(crop, (self.W, self.H), interpolation=cv2.INTER_LINEAR)
        else:
            # What an in-graph adapter would see: the C++ 48x320 black-padded canvas.
            # Recover the content width (last non-black column), crop, stretch to 128x64.
            canvas = cpp_contract_canvas(crop)
            cols = np.where(canvas.max(axis=(0, 2)) > 0)[0]
            cw = int(cols[-1]) + 1 if len(cols) else canvas.shape[1]
            img = cv2.resize(canvas[:, :cw], (self.W, self.H), interpolation=cv2.INTER_LINEAR)
        return self._run(np.ascontiguousarray(img[:, :, ::-1]))


def recognizers() -> dict:
    r = {}
    for n in ("cct-s-v2-global", "cct-xs-v2-global"):
        for mode in ("native", "contract"):
            r[f"fpo-{n}/{mode}"] = (lambda n=n, mode=mode: FastPlateOcr(n, mode))
    for n in ("en_PP-OCRv4_mobile_rec", "en_PP-OCRv5_mobile_rec", "latin_PP-OCRv5_mobile_rec"):
        for mode in ("native", "contract", "contract-graypad"):
            r[f"{n}/{mode}"] = (lambda n=n, mode=mode: PaddleRec(n, mode))
    return r
