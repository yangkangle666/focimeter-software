# M2 Experimental Multispot Synthetic Dataset

This directory is reserved for deterministic synthetic fixtures used by the experimental M2 Hartmann multispot detector. It contains no real LM700 images and must never be presented as metrology validation.

## Generator

The generator source is:

```text
focimeter_system/modules/image_recognition/tools/generate_multispot_synthetic.cpp
```

The CMake target `m2_generate_multispot_synthetic` generates this dataset with:

```powershell
m2_generate_multispot_synthetic --output focimeter_system/data/mock/m2_image_recognition/synthetic_multispot
```

The generator uses a fixed seed (`20260727`), writes 1280x1024 grayscale PNG images (8-bit plus one 12-bit-in-16-bit-container pair), JSON input packages, and `manifest.json`.

## Dataset Layout

```text
calibration/  clean 25-point and 94-point reference images
measurement/  shift, known-prescription, local-deformation, noisy-gradient, and low-contrast measurements
failure/      missing, extra, merged, edge-clipped, undersized-area, blank-dark, and blank-bright inputs
packages/     M1-shaped input_package JSON files for each case
manifest.json generated ground truth, transformations, and expected outcomes
```

The generated input packages deliberately use paths relative to `focimeter_system/`, such as:

```text
data/mock/m2_image_recognition/synthetic_multispot/calibration/25_clean_reference.png
```

They therefore contain no personal absolute path. To use a generated package with the M2 CLI, generate the data in this exact repository directory and pass `--project-root focimeter_system`.

## Test Intent

The fixture set includes:

- 25 clean spots with a measurement-frame translation.
- The same 25-point translation represented as 12-bit data in a 16-bit PNG container.
- 94 clean spots with a smooth local deformation.
- 94 clean spots transformed for the known prescription `S=-2.00 D, C=-1.00 D, A=45 degrees`.
- 94 spots with non-uniform background and noise.
- 25 low-contrast spots.
- 25 spots with an independent brightness change.
- Failing or warning-oriented inputs: 11 spots, 151 spots, merged spots, an edge-clipped spot, 25 deliberately undersized connected components, blank dark, and blank bright images.

`manifest.json` records known synthetic centers, but `synthetic_point_id` is local ground-truth bookkeeping only. It is not a formal `spot_id` and does not claim that a cross-image physical-ray identity has been established. Cross-image matching and M3 consumption require a separately approved multispot interface.

The known-prescription case is the rendered-image software integration path used by
`focimeter_system/run_synthetic_e2e.py`. It is simulation-only and does not represent a certified lens.

The manifest records actual existing M2 error codes and expected detector warnings. These expectations do not add new shared v1 error codes.
