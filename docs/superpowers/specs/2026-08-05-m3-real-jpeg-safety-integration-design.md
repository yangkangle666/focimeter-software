# M3 Real JPEG Safety Integration Design

## Objective

M3 consumes the four `m2.multispot.experimental.1` JSON documents introduced by M2 PR #10 without moving physical-ray matching into M2. It must accept the real-data metadata contract, reject unsafe detections, treat the approximately 42-degree lattice direction as an installation direction rather than an inter-frame rotation, and reject the complete calculation whenever any measurement detection lacks one unique physical identity.

## Ownership Boundary

- M2 detects spots independently in each image and emits image-local `detection_id`, coordinates, confidence, and quality diagnostics.
- M3 validates those outputs, establishes cross-image correspondence, and creates internal `spot_id` values only after one complete and unique mapping passes every configured constraint.
- M3 never copies `detection_id` into `spot_id`, never calculates from a matched subset, and never asks M2 to pre-match or silently remove currently retained detections.

## Input Contract

The experimental schema accepts `validation_scope` values `simulation_only` and `software_only`. It continues to require `validation_status=software_verified` and `metrology_validated=false`, preserving the fixed synthetic 94-point fixtures while accepting PR #10 real JPEG outputs.

Only `SATURATED_PEAK` is identity-safe at the retained-spot level. Every other nonempty retained-spot quality flag is rejected unless a later approved contract explicitly adds it to the safe set. This includes `AREA_ABOVE_MEDIAN`, `SUBPITCH_FRAGMENT_NEIGHBOR_REJECTED`, `NEARBY_CANDIDATE_UNRESOLVED`, `POSSIBLE_MERGED_COMPONENT`, and `LOW_CONFIDENCE`.

## Matching Geometry

Per-image lattice recovery may use any absolute orientation. `_candidate_bases` therefore does not apply `max_rotation_degree` to a single image. The configured rotation limit remains enforced only on the fitted calibration-to-measurement transform in `_fit_candidate`.

All retained measurement detections must participate in the winning one-to-one mapping. If the best integer-topology hypothesis maps only 23 of 27 detections, M3 raises `CoordinateSystemError` with the observed count before any prescription calculation. Reference-image edge loss remains supported only in the already approved reference-to-measurement direction and coverage limit; no measurement detection may be silently discarded.

Reflection, relative rotation over 15 degrees, exact 90-degree aliases, integer-pitch aliases, low confidence, low reference coverage, excessive residual, or a non-unique hypothesis continue to fail with `COORDINATE_SYSTEM_INVALID`.

## Error Contract

Unsafe quality flags and incomplete or ambiguous matching use the existing unified M3 error envelope. An error response contains no `lens_type`, `result`, `S`, `C`, or `A`. It is not a degraded prescription and remains recoverable only through corrected input or a future approved contract.

## Cross-Module Regression

M3 tests read these PR #10 files directly:

```text
focimeter_system/modules/image_recognition/samples/real_jpeg_software_verified/pair_1/spots_calib_multispot.json
focimeter_system/modules/image_recognition/samples/real_jpeg_software_verified/pair_1/spots_meas_multispot.json
focimeter_system/modules/image_recognition/samples/real_jpeg_software_verified/pair_2/spots_calib_multispot.json
focimeter_system/modules/image_recognition/samples/real_jpeg_software_verified/pair_2/spots_meas_multispot.json
```

The original pairs fail on unsafe retained-spot flags. A deep-copied pair 2 with all point flags and document warnings cleared reaches geometry matching, reports maximum topology overlap 23/27, and still fails the whole calculation. Synthetic tests prove a common 42-degree installation angle with a small relative rotation succeeds, while reflection, 90-degree alias, and relative rotation over 15 degrees fail.

## CI and Validation

The M3 workflow runs for pull requests targeting `task/m2-w0rry-real-jpeg-detection` and watches the real JPEG sample JSON path. Acceptance requires the full M3 suite, the M2 suite, shared mock validation, and `git diff --check`. These results establish software integration only and do not claim physical-ray truth or metrology accuracy for the real samples.
