# Real multispot lens pair set 001

This fixture contains one confirmed no-lens reference image and two real
lens measurement images. Each measurement has a matching commercial
focimeter screen photo.

The screen values are device references only. They are not certified
standard-lens values and must not be used to claim metrology accuracy.

## Cases

| Case | Device reference | Expected matching behavior |
| --- | --- | --- |
| lens_001 | S=-5.25 D, C=-2.00 D, A=154 degree | Strong anisotropic expansion with edge spots leaving the frame |
| lens_002 | S=-1.50 D, C=0.00 D | Moderate expansion with partial edge loss |

manifest.json records the exact file hashes, confirmed pairing, known
readings, engineering-only spot-count estimates, and missing acquisition
metadata.

## Run M2

Run from the repository root after building focimeter_m2:

    focimeter_m2 --input focimeter_system/data/real/multispot_lens_pairs/real_lens_pair_set_001/packages/input_package_lens_001.json --output focimeter_system/outputs/real_lens_pair_set_001_lens_001 --project-root focimeter_system --experimental-multispot --save-intermediate

    focimeter_m2 --input focimeter_system/data/real/multispot_lens_pairs/real_lens_pair_set_001/packages/input_package_lens_002.json --output focimeter_system/outputs/real_lens_pair_set_001_lens_002 --project-root focimeter_system --experimental-multispot --save-intermediate

The runtime output directory is intentionally ignored by Git. Reviewed
M2 JSON outputs that become stable integration fixtures should be copied
into this dataset under a dedicated m2_outputs directory.

## M3 acceptance boundary

The reference and measurement detection counts are expected to differ
because lens deformation moves edge spots outside the image. M3 matching
must support partial overlap, reject ambiguous correspondences, and only
assign internal physical-ray identities after matching. It must not rename
image-local detection_id values to spot_id.

The supplied detection config declares the known image size and leaves
pixel size, optical distance, Hartmann spacing, and wavelength unknown.
It is suitable for M2 detection only and is intentionally not
calculation-ready for M3.
