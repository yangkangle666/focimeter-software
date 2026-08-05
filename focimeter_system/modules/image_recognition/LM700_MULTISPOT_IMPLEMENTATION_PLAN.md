# LM700 / Hartmann Experimental Multispot Implementation Plan

> This plan implements the user-approved stage-three goal. The multispot path is experimental and must not replace or impersonate the five-spot v1 contract.

**Goal:** Preserve the PR #4 five-spot compatibility baseline while adding an explicit, software-only Hartmann multispot detection mode with isolated JSON output, reproducible synthetic fixtures, regression tests, and interface documentation.

**Architecture:** `five_spot_compat` remains the default and keeps the current matcher and v1 output files. `hartmann_multispot` is enabled only by `--experimental-multispot`, reuses common image loading, ROI, diagnostics, output locking, atomic publication, and intermediate artifacts, but uses a dedicated detector and serializer. Multispot IDs are deterministic per-file row-major indices only; cross-image physical identity is explicitly not guaranteed.

**Tech stack:** C++17, OpenCV, nlohmann/json, CMake 3.21+, Visual Studio 2022, CTest.

## Global Constraints

- Do not modify `interface_contract_v1.md`, `default_config.json`, M1, M3, M4, governance documents, or teacher-provided material.
- Do not connect hardware or install drivers/dependencies.
- Do not calculate S/C/A, displacement fields, wavefronts, or optical power.
- Keep all multispot outputs under `experimental_multispot/` and label them `experimental`, `proposed`, `software_verified`, `software_only`, and `metrology_validated=false`; use `data_source` to distinguish synthetic, mock, and real file inputs.
- Reject ambiguous or unsafe conditions; never claim row-major IDs are physical-ray identities.
- Preserve all current five-spot Debug and Release tests.
- Do not commit build artifacts, caches, teacher materials, archives, secrets, or personal absolute paths.

## File Responsibilities

- `include/focimeter/m2/types.h`: recognition mode, internal experimental parameters, multispot observation diagnostics, mode-aware result metadata.
- `include/focimeter/m2/image_processor.h`, `src/image_processor.cpp`: common preparation plus a dedicated experimental connected-component detector inspired by the LM700 MATLAB research reconstruction.
- `include/focimeter/m2/json_io.h`, `src/json_io.cpp`: isolated multispot success/error JSON serialization without changing v1 serialization.
- `src/module.cpp`: mode-aware output paths, five-spot/multispot branching, atomic publication, artifact saving, and run logging.
- `src/focimeter.cpp`: explicit `--experimental-multispot` CLI switch and mode-aware display paths.
- `tools/generate_multispot_synthetic.cpp`: deterministic multispot image, package, and manifest generation.
- `tests/test_multispot.cpp`: detector, JSON, CLI-facing module behavior, failure cases, and coordinate-ground-truth tests.
- `CMakeLists.txt`: build/register the new generator and multispot tests while retaining existing targets.
- `README.md`, `CHANGELOG.md`, `MULTISPOT_INTERFACE_PROPOSAL.md`, `LM700_MULTISPOT_MIGRATION_NOTES.md`: usage, compatibility, evidence, limits, and approval questions.
- `data/mock/m2_image_recognition/synthetic_multispot/`: generated small fixtures, input packages, ground truth manifest, and data README.

## Task 1: Freeze Compatibility and Add Mode Types

- [x] Add `RecognitionMode::{FiveSpotCompat,HartmannMultispotExperimental}` with `FiveSpotCompat` as the default.
- [x] Add internal multispot limits and research parameters without reading or changing shared configuration fields.
- [x] Extend observations and diagnostics with peak, integrated intensity, threshold, background, rejected counts, and quality flags while leaving v1 serialization unchanged.
- [x] Add tests proving default construction still selects five-spot behavior.

## Task 2: Implement Experimental Detection

- [x] Write tests for clean 25-point and 94-point arrays, low count, over-limit count, blank images, whole-image coordinates, confidence bounds, and per-file sorting.
- [x] Reuse image loading, explicit 8/16-bit conversion, ROI, grayscale conversion, raw-image exposure diagnostics, filtering, top-hat enhancement, and intermediate matrices.
- [x] Estimate background from border pixels, derive an automatic threshold, produce a foreground mask, find connected components, filter area/border candidates, and calculate background-subtracted intensity-weighted centroids.
- [x] Reject low-count and over-limit results with existing contract-safe error codes plus structured details; do not silently truncate.
- [x] Mark suspicious merged/edge/low-confidence observations with experimental quality flags or warnings.
- [x] Sort accepted observations by `y`, then `x`, only for deterministic file-local indexing.
- [x] Run focused multispot detector tests, then the existing image tests.

## Task 3: Isolate Experimental JSON and Module Flow

- [x] Add module tests for experimental output paths and mandatory metadata.
- [x] Add experimental success and error serializers without relaxing `writeSpotSuccess` five-point checks.
- [x] Publish experimental files as `experimental_multispot/spots_calib_multispot.json`, `spots_meas_multispot.json`, and `m2_multispot_run_log.json`.
- [x] Include all observations and quality diagnostics while setting `physical_identity_guaranteed=false` and `owner_status=unassigned`.
- [x] Bypass `SpotMatcher` only in experimental mode; retain it unchanged in five-spot mode.
- [x] Preserve paired atomic output publication, stale-output invalidation, path-alias guards, output locking, standard exit-code classes, and intermediate artifacts.
- [x] Run module integration tests for success, one-image failure, dimension mismatch, and unwritable output.

## Task 4: Add Explicit CLI Selection

- [x] Add `--experimental-multispot` to parsing and `--help`.
- [x] Keep no-flag behavior compatible at the public contract level.
- [x] Make `--show` load annotations from the selected mode's managed output directory.
- [x] Register CLI success and failure tests for both modes.

## Task 5: Generate Reproducible Synthetic Data

- [x] Implement one deterministic generator that writes images, input packages, manifest, and expected metadata together.
- [x] Generate clean 25-point and synthetic 94-point stress arrays; explicitly state that 94 points are not confirmed LM700 physical holes.
- [x] Generate shifted, locally deformed, brightness-changed, gradient, noisy, low-contrast, 12-bit-in-16-bit, missing, extra, merged, edge-clipped, undersized-area, dark, and bright cases.
- [x] Record the fixed seed, known centers, expected mode/count, transform, warnings/error, and `metrology_validated=false`.
- [x] Verify repeated generation is byte-identical and matches the committed fixtures.

## Task 6: Complete Regression and Documentation

- [x] Keep existing v1 five-spot JSON shape checks strict and separate from experimental JSON checks.
- [x] Run the existing 25-point failure fixture in five-spot mode to prove compatibility behavior remains unchanged.
- [x] Run Debug and Release configure/build/CTest, CLI success/failure cases, and the repository mock validator.
- [ ] Re-run the M3 five-point validator. Safely skipped: both the system Python and bundled Codex Python lack its declared `jsonschema` dependency, while dependency installation is prohibited. PR #4's earlier result is historical evidence, not a new stage-three run.
- [x] Document why the connected-component research path was selected for this stage and record the local-maximum path as an unimplemented comparison candidate.
- [x] Explain M2/M3 ownership, experimental IDs, public-contract conflicts, real-data blockers, and parameters that remain research defaults.
- [x] Update README and CHANGELOG only with results actually observed.

## Task 7: Review and Delivery

- [x] Run independent architecture, interface, algorithm, test/data, compatibility, Git-scope, and documentation reviews.
- [x] Resolve all P0/P1 issues and either resolve or document P2 issues; final technical reviews report `P0=0` and `P1=0`.
- [x] Run `git diff --check`, inspect every changed file, scan for secrets/absolute paths/build outputs, and stage all 50 approved paths individually.
- [x] Recheck `git fetch origin --prune`, PR #4 status, and remote branch history; PR #4 remains Draft/Open at baseline `2938ce7`, and the stage-three branch was pushed without rewriting history.
- [x] Commit the implementation as `5462c05`, push only to `origin/task/m2-w0rry-lm700-multispot`, and create ready-for-review stacked PR #5 against `task/m2-w0rry-software-verification`. Do not merge.
- [x] Produce the required final report and PR description from the recorded build, test, review, commit, and push evidence.

## Delivery Record

- Implementation commit: `5462c0559dc7f15746ffd365b36394eadbe5958f`
- Remote branch: `origin/task/m2-w0rry-lm700-multispot`
- Ready-for-review stacked PR: <https://github.com/yangkangle666/focimeter-software/pull/5>
- PR base: `task/m2-w0rry-software-verification`
- PR dependency: PR #4
- Merge status: not merged
