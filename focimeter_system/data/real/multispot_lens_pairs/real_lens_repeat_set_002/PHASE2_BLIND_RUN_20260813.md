# Phase-two blind run - 2026-08-13

## Result

The first parameter-unchanged blind run used all 12 packages in this dataset.

| Stage | Result |
| --- | --- |
| M1 path and config validation | 12/12 passed after correcting two dataset config enum values |
| M2 experimental multispot detection | 0/12 passed; all cases returned `SPOT_COUNT_MISMATCH` |
| M3 matching and calculation | Not run because no case produced successful M2 documents |

This run did not change M1, M2, or M3 source code and did not tune detection
parameters. The current blocker is M2 candidate generation on this new image
family. It is not an M3 result and must not be assigned to M3 until M2 produces
valid calibration and measurement documents.

## M2 diagnostics

The safety limit is 150 pre-filter candidates. The current small-scale path
uses an 8-bit threshold of 8 for every image and segments background texture
into many small connected components.

| Case | Reference raw / pre-filter | Measurement raw / pre-filter |
| --- | ---: | ---: |
| lens_001 repeat 01 | 723 / 325 | 2374 / 1299 |
| lens_001 repeat 02 | 723 / 325 | 2135 / 1112 |
| lens_001 repeat 03 | 723 / 325 | 3715 / 2186 |
| lens_001 repeat 04 | 723 / 325 | 4738 / 2808 |
| lens_002 repeat 01 | 349 / 169 | 5152 / 3081 |
| lens_002 repeat 02 | 349 / 169 | 4967 / 2954 |
| lens_002 repeat 03 | 349 / 169 | 4208 / 2396 |
| lens_002 repeat 04 | 349 / 169 | 4656 / 2707 |
| lens_003 repeat 01 | 352 / 165 | 4827 / 2834 |
| lens_003 repeat 02 | 352 / 165 | 4367 / 2507 |
| lens_003 repeat 03 | 352 / 165 | 3919 / 2222 |
| lens_003 repeat 04 | 352 / 165 | 3005 / 1583 |

The main spots are visually clear. The binary diagnostics also contain many
background fragments, and the measurement images include weaker lens-produced
spot structure near the center. Raising the candidate limit would pass unsafe
noise into lattice recovery and is not an acceptable fix.

## M2 repair task and acceptance criteria

M2 owns the next implementation task within
`focimeter_system/modules/image_recognition/`. The change must improve
candidate generation or evidence-based pre-filtering for this image family.
It must not hard-code filenames, lens numbers, coordinates, expected S/C/A, or
per-image thresholds.

Acceptance requires all of the following:

1. Run all 12 packages with the same approved configuration and produce valid
   `m2.multispot.experimental.1` calibration and measurement documents, or
   provide a specific evidence-based rejection for an individual bad capture.
2. Keep the 150-candidate safety boundary unless a separately reviewed design
   proves a safe replacement. Do not solve the failure by only raising it.
3. Preserve obvious main spots while rejecting background texture before the
   lattice-analysis safety gate.
4. Add representative regression coverage from this dataset without embedding
   case-specific coordinates or prescription values.
5. Preserve the existing M2 test baseline (`17/17`) and deterministic output
   behavior.
6. Do not modify M3 source. After M2 succeeds, the project lead will rerun M3
   on the actual M2 output and assign any new matching/calculation failure to
   the responsible module based on that result.

## Baseline regression

- M2 CTest: 17/17 passed.
- M3 unittest suite: 130/130 passed.
- `metrology_validated=false` remains unchanged.

Runtime logs and intermediate images remain under
`focimeter_system/outputs/phase2_blind_20260813_125933/` and are intentionally
not versioned. This report records the durable result needed by all members.
