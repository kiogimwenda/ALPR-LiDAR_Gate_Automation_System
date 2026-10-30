# ADR-014: ALPR Model Selection, Licensing and Provenance

## Status
Accepted. This amends [ADR-010](ADR-010-model-licensing.md): ADR-010 assumed we
would fine-tune YOLOv9 ourselves, and this ADR records which third-party weights
actually ship.

## Date
2026-10-22

## Context

The server's two-stage ALPR pipeline (detector → plate OCR on TensorRT) had no
trained weights. We evaluated public candidates on a Kenya test set built for
the purpose:

- 166 Wikimedia Commons photos taken in Kenya (CC BY, CC BY-SA, CC0).
- 225 plates, all read by hand.
- Every image swept by hand for plates no detector found.
- Committed manifest: `scripts/export-models/eval/testset/`.
- Re-download with SHA-256 checks: `fetch_testset.py`.

The full results are in `models/downloads/eval/RESULTS.md`, which is generated
and not committed. The project is GPL-3.0 ([ADR-000](ADR-000-license.md)).

| Candidate | Detection R / P @IoU 0.5 | Result |
|---|---|---|
| open-image-models YOLOv9-s-608 @0.15 | 0.889 / 0.935 | **chosen** |
| open-image-models YOLOv9-t-640 @0.10 | 0.862 / 0.878 | runner-up |
| Topurrra RT-DETRv2-r18 @0.08 | 0.773 / 0.906 | documented fallback (Apache-2.0) |
| morsetechlab YOLOv11s (Ultralytics) | 0.609 / 0.901 | **rejected**: AGPL-3.0, and the weakest |

The recognizer numbers below are Kenya-normalised exact match.

| Candidate | On hand-labelled crops | End to end behind YOLOv9-s-608 @0.15 | Result |
|---|---|---|---|
| fast-plate-ocr cct-s-v2-global | 0.756 (two-line 0.583) | 0.716 (two-line 0.667) | **chosen** |
| fast-plate-ocr cct-xs-v2-global | 0.787 (two-line 0.667) | 0.698 (two-line 0.639) | statistically tied |
| PaddleOCR PP-OCRv4/v5 rec (en, latin) | 0.42–0.46 | ≤ 0.41 | rejected: CTC single-line, so 0% on two-line plates |

**S vs XS.** The tie-breaker was native-preprocessing end-to-end accuracy, and
then two-line accuracy. Between S and XS the disagreements split 14 to 10, which
is not significant.
- S wins end to end, both overall and on two-line plates.
- XS wins only on hand-labelled crops, not on detector crops, which is what
  production sees.
- XS is 20% faster on GPU (1.03 vs 1.25 ms) and much faster on CPU. Swapping
  needs only the ONNX file and its plate config.

## Decision

| | Detector | Recognizer |
|---|---|---|
| Model | open-image-models `yolo-v9-s-608-license-plates-end2end.onnx` | fast-plate-ocr `cct_s_v2_global.onnx` + `cct_s_v2_global_plate_config.yaml` |
| Source | github.com/ankandrew/open-image-models, release `assets` (SHA-256 pinned in `fetch_models.py`) | github.com/ankandrew/fast-plate-ocr, release `arg-plates` (SHA-256 pinned) |
| Code licence | MIT | MIT |
| Weight licence | No separate weight licence. The maintainer says the weights were trained with his fork of WongKinYiu/yolov9 (**GPL-3.0**) and that he is not a legal expert on whether the GPL reaches them ([issue #20](https://github.com/ankandrew/open-image-models/issues/20), [#22](https://github.com/ankandrew/open-image-models/issues/22)). **We treat them as GPL-3.0**, which ADR-010 already accepts for this GPL-3.0 project. | MIT. The maintainer wrote in [open-image-models#20](https://github.com/ankandrew/open-image-models/issues/20): *"from my end everything is MIT and you can use commercially, that includes source code here and any of the models/weights of https://github.com/ankandrew/fast-plate-ocr. No restrictions."* Repo LICENSE: MIT. |
| Training data | About 11k web-scraped images, "mostly from free/open datasets" ([#14](https://github.com/ankandrew/open-image-models/issues/14)). Not itemised. Not UFPR-ALPR ([#24](https://github.com/ankandrew/open-image-models/issues/24)). | Not disclosed. 66 regions, none African. |
| Our modification | `EfficientNMS_TRT` grafted onto the ONNX graph (weights untouched). Output SHA-256 `55ac0d58…a5ac71`. | None |

**Rejected:**
- **morsetechlab YOLOv11** (AGPL-3.0, Ultralytics). Its own model card says its
  Roboflow training data has a train/test leak.
- **Koushim YOLOv8** ("MIT" on Hugging Face, but trained with Ultralytics, so
  the AGPL claim is unresolved). Not evaluated.

## Provenance unknowns and contamination

- **Undisclosed training data, possible overlap with our test set.** Neither
  model's training images are itemised. The detector's web-scraped set could
  include Wikimedia Commons photos, which is where the test set comes from. The
  numbers above may therefore be optimistic.
- **Unverified licences on training images.** We cannot confirm that every
  image used to train these weights was licensed for that use.
- **GPL status of the detector weights is a legal interpretation.** We do not
  rely on any interpretation more permissive than GPL-3.0.

## Mitigations

1. **Pin exactly what we ship.** Upstream files and the graft output are pinned
   by SHA-256. No `.onnx` or `.plan` file is committed. Engines are built on the
   deployment host.
2. **Swappable models.** The detector sits behind the EfficientNMS_TRT contract
   and the recognizer behind `PlateRecognizer`'s `kSlots`/`kCtc` modes, so
   swapping either is an ONNX change plus at most a config flag.
3. **Retrain on our own data.** The follow-up below fine-tunes on site data we
   own. That removes both the contamination question and most of the
   data-provenance exposure.
4. **Permissive fallback detector.** If GPL weights ever become unacceptable
   (for example, a closed-source deployment), switch to **Topurrra
   RT-DETRv2-r18** (Apache-2.0, trained on Open Images annotations under
   CC BY 4.0) and fine-tune it on site data. Its single-class scores are
   compressed below about 0.15, and it needs a small decoder change (it outputs
   `logits` and `pred_boxes` with no NMS).

## Consequences

- The project stays GPL-3.0 with no new licence obligations. The recognizer is
  MIT.
- `gate-vision` defaults to `--recognizer-mode slots`. The PaddleOCR CTC path
  still works with a model whose output has 37 classes (blank plus the 36
  dictionary characters). A stock PP-OCR model has 97 or more and is rejected
  at load.
- **Expected accuracy.** About 72% of plates in Kenyan street photos are read
  exactly. On one-line plates the figure is about 75%. On two-line plates it is
  about 68%.
- **Known weak spots:**
  - Two-line `KMxx` motorcycle plates are often read with "KM" as "KW".
  - A final letter is sometimes read as its look-alike digit (Z→2, Q→0, B→8);
    the Kenya normaliser repairs most of these.
- **Gate cameras will differ from the test photos.** Gate footage is closer
  range and night shots use infrared. None of this is covered by the test set.

## Follow-up: site fine-tuning (out of scope for this ADR)

1. **Collect.** Run gate-vision with frame and crop logging at 2–3 pilot gates
   for 2–4 weeks, covering day, night (IR) and rain. Target **2–5k plate
   crops**:
   - weighted towards two-line plates and `KMxx` / `KTWx` motorcycle and taxi
     plates (at least 30% combined);
   - plus GK and new-style flag plates.

   Collect under the site's CCTV and data-protection notice, and store crops
   only.
2. **Auto-label and correct.** Pre-label each crop with the current pipeline
   (raw text, normalised text, scores). Have a human confirm or correct it in a
   simple review tool. Prioritise reads that are low-confidence, rejected by
   the normaliser, or disagree between frames of the same vehicle.
3. **Train.**
   - **Recognizer:** fine-tune `cct-s-v2-global` with fast-plate-ocr's
     fine-tune workflow. Keep the alphabet `0-9A-Z_` so the C++ contract does
     not change.
   - **Detector:** fine-tune on site frames with boxes drawn from the confirmed
     crops. To leave the GPL training-code question behind, fine-tune
     RT-DETRv2 rather than YOLOv9.
4. **Hold out a test set.** Keep a site test set of at least 500 plates from
   cameras and days never used in training, including night and IR. Report the
   same metrics as `eval_candidates.py` and `cpp_parity.py`, and gate the model
   swap on beating this ADR's numbers.
