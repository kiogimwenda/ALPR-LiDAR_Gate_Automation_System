# Kenya ALPR test set: licensing and provenance

`manifest.csv` describes 166 photographs from Wikimedia Commons and 225
plates read by hand from them. The text files next to it hold the hand
annotations the manifest is built from:

- `gt_notes.txt`: review of the detectors' proposals;
- `sweep_notes.txt` and `manual_boxes.txt`: the full-image recall sweep.

`../build_testset_manifest.py` documents the format and the build process.

## Images: not redistributed here

The photographs are **not part of this repository**. `../fetch_testset.py`
downloads each one from its `image_url` on Wikimedia Commons into the
git-ignored `models/downloads/kenya-testset/images/`, and checks every file's
`sha256` against the manifest.

Each image is under the licence its author chose:

- The licence is in the manifest's `licence` and `licence_url` columns.
- The author is in the `author` column.
- The Commons file page is in `source_page`.

The licences are CC0, CC BY 2.0/3.0/4.0 and CC BY-SA 2.0/3.0/4.0. Anyone who
redistributes or publishes an image, or a derivative such as the annotated
images `render_eval.py` produces, must follow that image's licence: credit
the author, link the licence, and for CC BY-SA share alike.

Photos where a person's face is the main subject were left out.

## Annotations: ours

The annotations are original work by this project and are licensed under the
project licence (GPL-3.0, see `LICENSE` at the repository root):

- the plate text strings;
- the box coordinates;
- the ok / ignore / no labels;
- the per-image metadata in `manifest.csv` and the annotation notes.

The plate strings were transcribed from photographs that their authors had
already published on Wikimedia Commons. They are licence plates of vehicles on
public streets, with no link to any other personal data. They are used only to
measure plate-reading accuracy.

## When a Commons URL drifts

Commons files can be renamed or renumbered, and the same title can later
serve a different photo. `fetch_testset.py` checks every download against the
manifest's `sha256` and deletes any file that does not match, so a drifted URL
can never silently change the test set.

This has happened once. After the harvest on 2026-10-19, ke155 moved from
"…Uganda 03.jpg" to "…Uganda 01.jpg". We found the photo again under the new
title, confirmed the bytes are identical (same SHA-256) and that the licence
and author are unchanged, and updated the manifest's `source_page` and
`image_url`.
