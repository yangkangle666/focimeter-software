# Real multispot repeated-capture set 002

This dataset is the confirmed subset of the real images received on
2026-08-13. It contains three lens cases. Each case has its own no-lens
reference image, four repeated captures of the same stationary lens, a
commercial focimeter screen photo, and a package-label photo.

Use this dataset for phase-two software integration and repeatability checks.
Do not use it to claim metrology accuracy.

## Confirmed cases

| Case | Commercial device reference | Repeats | Notes |
| --- | --- | ---: | --- |
| lens_001 | S=-1.50 D, C=+0.00 D, displayed A=0 degree | 4 | A is not meaningful when C=0 |
| lens_002 | S=-1.13 D, C=-2.00 D, A=76 degree | 4 | A is an engineering reference |
| lens_003 | S=-2.50 D, C=-1.88 D, A=70 degree | 4 | A is an engineering reference |

The original fourth case is intentionally excluded because its package label
and commercial-device reading could not be confirmed as belonging to the same
lens. It must not be silently added back to this dataset.

## Acquisition facts supplied by the project lead

- The received spot images are 2560 x 1440 JPEG files.
- The camera and optical assembly stayed fixed during acquisition.
- The four repeated measurement images in each case were taken without moving
  or repositioning that lens.
- Before and after placing a lens, the operator manually aligned the crosshair
  to the centroid of the largest central spot. The original instrument uses
  the same target, but locates it algorithmically.
- The camera was described as 5 MP with a 1/2.7-inch sensor. Exact pixel pitch,
  exposure, gain, optical magnification, Hartmann spacing, and optical distance
  were not supplied as numeric calibration values.

## Directory layout

```text
config/detection_config.json
images/lens_001/reference_no_lens.jpg
images/lens_001/measurement_01.jpg ... measurement_04.jpg
images/lens_002/...
images/lens_003/...
labels/lens_001_package.jpg ... lens_003_package.jpg
readings/lens_001_device.jpg ... lens_003_device.jpg
packages/input_package_lens_001_repeat_01.json ... repeat_04.json
manifest.json
```

`manifest.json` is authoritative for case pairing, device values, acquisition
claims, exclusions, and SHA-256 hashes. Package-label values are nominal
product labels; commercial focimeter values are engineering comparison values.
Neither is a certified standard-lens truth value.

## Run M2

Build `focimeter_m2`, then run a package from the repository root. Example:

```text
focimeter_m2 --input focimeter_system/data/real/multispot_lens_pairs/real_lens_repeat_set_002/packages/input_package_lens_001_repeat_01.json --output focimeter_system/outputs/real_lens_repeat_set_002_lens_001_repeat_01 --project-root focimeter_system --experimental-multispot --save-intermediate
```

Repeat for the remaining packages. Runtime output belongs under
`focimeter_system/outputs/` and is intentionally ignored by Git.

## Validation boundary

The initial blind run should keep the current code and parameters unchanged.
Record whether M1, M2, and M3 complete, M2 detection counts, M3 unique-match
counts, all four S/C/A results per lens, and within-lens repeatability. Diagnose
failures before changing parameters. Do not tune the implementation only to
these 12 images.

All results remain `software_verified`, `software_only`, and
`metrology_validated=false` until hardware calibration values and certified
standard-lens measurements are available.
