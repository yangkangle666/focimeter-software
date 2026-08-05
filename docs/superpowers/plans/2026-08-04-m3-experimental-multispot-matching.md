# M3 Experimental Multispot Matching Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Accept M2 experimental multispot detections, require a unique one-to-one calibration/measurement identity for every detected point, assign internal physical-ray IDs only after full matching, and then reuse the existing M3 S/C/A calculation.

> **2026-08-05 safety decision:** The project owner superseded the original partial-overlap behavior. Unequal counts or any unmatched detection must now return `COORDINATE_SYSTEM_INVALID`; a matchable subset cannot be used to calculate a prescription.

**Architecture:** A strict experimental-input adapter produces observations without `spot_id`. A dedicated lattice matcher recovers per-image topology, enumerates overlap hypotheses, validates each with constrained weighted affine fitting, and returns paired v1-shaped in-memory documents plus diagnostics. The existing calculator dispatches by schema and only sees paired documents after the experimental matcher succeeds.

**Tech Stack:** Python 3.12, NumPy 2.x, jsonschema 4.x, unittest, existing M3 JSON schemas and calibration model.

## Global Constraints

- Modify M3 files and M3-owned test fixtures only; do not modify M2 source code.
- Accept experimental inputs only when `schema_version == "m2.multispot.experimental.1"`.
- Treat `detection_id` as image-local diagnostic metadata, never as cross-image identity.
- Reject partial overlap such as `43 -> 27` and `43 -> 39`; every detected point must be uniquely matched.
- Generate internal `spot_id` only after one unique, validated match hypothesis exists.
- Reject ambiguous 90/180/270-degree, mirrored, integer-pitch, low-confidence, degenerate, or high-residual matches with `COORDINATE_SYSTEM_INVALID`.
- Keep all thresholds in the M3 calibration model's `matching_limits` object.
- Preserve the existing v1 paired-`spot_id` path and all 74 existing M3 tests.
- Keep experimental and real-image results at `software_verified` or `simulation_only`; never claim `metrology_validated` without certified data.

---

### Task 1: Experimental Input Contract and Adapter

**Files:**
- Create: `focimeter_system/modules/calibration_calculation/schemas/m2_multispot_experimental.schema.json`
- Create: `focimeter_system/modules/calibration_calculation/algorithm/experimental_input.py`
- Create: `focimeter_system/modules/calibration_calculation/tests/test_experimental_input.py`

**Interfaces:**
- Produces: `ExperimentalObservation`, `ExperimentalPair`, and `parse_experimental_pair(calibration, measurement) -> ExperimentalPair`.
- Guarantees: returned observations contain no `spot_id`; `detection_id` remains diagnostic only.

- [ ] **Step 1: Write failing schema and adapter tests**

```python
def test_detection_ids_remain_image_local():
    calibration = experimental_document("calibration", [91, 7, 42])
    measurement = experimental_document("measurement", [4, 88, 2])
    pair = parse_experimental_pair(calibration, measurement)
    assert [item.detection_id for item in pair.calibration] == [91, 7, 42]
    assert not hasattr(pair.calibration[0], "spot_id")

def test_nonempty_quality_flags_are_rejected():
    calibration = experimental_document("calibration", [0, 1, 2, 3])
    calibration["spots"][1]["quality_flags"] = ["EDGE_CLIPPED"]
    with pytest_compatible_assert_raises(CoordinateSystemError):
        parse_experimental_pair(calibration, experimental_document("measurement", [0, 1, 2, 3]))
```

- [ ] **Step 2: Run the new tests and verify import failure**

Run: `python -m unittest modules.calibration_calculation.tests.test_experimental_input -v`

Expected: FAIL because `experimental_input` does not exist.

- [ ] **Step 3: Add the strict experimental JSON schema**

The schema must require the exact experimental envelope fields, unique integer `detection_id` values, finite-number-compatible `x/y`, confidence in `[0, 1]`, `quality.is_usable=true`, and `matching.physical_identity_guaranteed=false`. It must not define a `spot_id` property.

- [ ] **Step 4: Implement immutable adapter types and validation**

```python
@dataclass(frozen=True)
class ExperimentalObservation:
    detection_id: int
    x: float
    y: float
    confidence: float

@dataclass(frozen=True)
class ExperimentalPair:
    task_id: str
    calibration: tuple[ExperimentalObservation, ...]
    measurement: tuple[ExperimentalObservation, ...]

def parse_experimental_pair(
    calibration: Mapping[str, object],
    measurement: Mapping[str, object],
) -> ExperimentalPair:
    """Validate two M2 experimental documents without creating physical IDs."""
```

Use `Draft202012Validator`, reject non-finite values after schema validation, require matching task IDs and image types, require unique IDs within each image, and reject any nonempty `quality_flags`.

- [ ] **Step 5: Run adapter tests and commit**

Run: `python -m unittest modules.calibration_calculation.tests.test_experimental_input -v`

Expected: PASS.

Commit: `feat(m3): accept experimental multispot detections`

### Task 2: Matching Limits in the M3 Calibration Model

**Files:**
- Modify: `focimeter_system/modules/calibration_calculation/algorithm/types.py`
- Modify: `focimeter_system/modules/calibration_calculation/schemas/calibration_model.schema.json`
- Modify: `focimeter_system/modules/calibration_calculation/examples/calibration/calibration_model.simulation.json`
- Modify: `focimeter_system/modules/calibration_calculation/tests/test_algorithm_schemas.py`
- Modify: `focimeter_system/modules/calibration_calculation/tests/test_calibration_algorithm.py`

**Interfaces:**
- Produces: `MatchingLimits` and `CalibrationModel.matching_limits`.
- Consumed by: `match_experimental_multispot` in Task 3.

- [ ] **Step 1: Add failing round-trip and invalid-limit tests**

```python
def test_matching_limits_round_trip():
    model = CalibrationModel.from_dict(load_simulation_model())
    assert model.matching_limits.min_matched_spots == 12
    assert model.to_dict()["matching_limits"]["max_rotation_degree"] == 15.0

def test_half_pitch_translation_bound_is_enforced():
    data = load_simulation_model()
    data["matching_limits"]["max_translation_pitch_ratio"] = 0.5
    with self.assertRaises(ModelError):
        CalibrationModel.from_dict(data)
```

- [ ] **Step 2: Verify the tests fail because the field is absent**

Run: `python -m unittest modules.calibration_calculation.tests.test_algorithm_schemas modules.calibration_calculation.tests.test_calibration_algorithm -v`

Expected: FAIL on missing `matching_limits`.

- [ ] **Step 3: Add `MatchingLimits` with strict invariants**

```python
@dataclass(frozen=True)
class MatchingLimits:
    min_matched_spots: int
    min_overlap_ratio: float
    max_lattice_residual_pitch_ratio: float
    max_matching_rmse_pitch_ratio: float
    max_matching_residual_pitch_ratio: float
    max_condition_number: float
    max_rotation_degree: float
    min_scale_ratio: float
    max_scale_ratio: float
    max_shear_ratio: float
    max_translation_pitch_ratio: float
    min_confidence: float
    minimum_hypothesis_margin: float
```

Require `min_matched_spots >= 4`, ratios in documented positive ranges, `max_translation_pitch_ratio < 0.5`, `max_rotation_degree < 45`, and ordered scale limits containing `1.0`.

- [ ] **Step 4: Add schema and simulation-only values**

Use conservative software-test values: 12 matched points, 0.12 lattice residual/pitch, 0.10 matching RMSE/pitch, 0.25 max residual/pitch, condition number 50, 15 degrees rotation, scale `[0.75, 1.25]`, shear 0.2, translation 0.45 pitch, confidence 0.35, and hypothesis margin 0.15. The legacy `min_overlap_ratio=0.6` model field remains for backward compatibility, but the 2026-08-05 safety gate independently requires `1.0` full coverage.

- [ ] **Step 5: Run model tests and commit**

Expected: model schema and round-trip tests PASS.

Commit: `feat(m3): define conservative multispot matching limits`

### Task 3: Full-Coverage Lattice Matcher

**Files:**
- Create: `focimeter_system/modules/calibration_calculation/algorithm/multispot_matching.py`
- Create: `focimeter_system/modules/calibration_calculation/tests/test_multispot_matching.py`

**Interfaces:**
- Consumes: `ExperimentalPair`, `MatchingLimits`.
- Produces: `MatchDiagnostics`, `MatchedExperimentalPair`, and `match_experimental_multispot(pair, limits) -> MatchedExperimentalPair`.

- [ ] **Step 1: Add deterministic synthetic lattice helpers and failing success tests**

```python
def test_partial_overlap_43_to_27_rejects():
    pair = make_partial_pair(calibration_count=43, measurement_count=27, reorder=True)
    with self.assertRaises(CoordinateSystemError):
        match_experimental_multispot(pair, limits())

def test_detection_id_permutation_does_not_change_pairs():
    original = match_experimental_multispot(make_pair(), limits())
    permuted = match_experimental_multispot(make_pair(randomize_ids=True), limits())
    assert physical_coordinate_pairs(original) == physical_coordinate_pairs(permuted)
```

- [ ] **Step 2: Run tests and verify matcher import failure**

Run: `python -m unittest modules.calibration_calculation.tests.test_multispot_matching -v`

Expected: FAIL because the matcher does not exist.

- [ ] **Step 3: Implement robust per-image pitch and basis estimation**

Implement focused private functions with these exact interfaces: `_estimate_pitch(points: np.ndarray) -> float`, `_candidate_bases(points: np.ndarray, pitch: float, limits: MatchingLimits) -> tuple[np.ndarray, ...]`, and `_assign_lattice(points: np.ndarray, basis: np.ndarray, limits: MatchingLimits) -> LatticeAssignment`.

Use pairwise distances only to estimate local pitch, derive non-collinear short-vector bases, round projected coordinates, and reject coordinate collisions or excessive normalized reconstruction residual.

- [ ] **Step 4: Implement overlap hypothesis enumeration and affine scoring**

Implement `_candidate_matches(calibration: LatticeAssignment, measurement: LatticeAssignment) -> Iterable[MatchHypothesis]` and `_fit_and_score(hypothesis: MatchHypothesis, pair: ExperimentalPair, limits: MatchingLimits) -> ScoredHypothesis | None`.

Enumerate axis swap, signs, and integer offsets. Fit a confidence-weighted affine map on the coordinate intersection. Reject insufficient overlap, rank loss, reflection, excess condition number, rotation, scale, shear, translation, RMSE, or maximum residual.

- [ ] **Step 5: Enforce uniqueness before assigning IDs**

Sort candidates by matched count, overlap ratio, and normalized residual. Reject when the best and second-best distinct physical mappings differ by less than `minimum_hypothesis_margin`. Build continuous `spot_id` values from the winning calibration lattice order only after this check.

- [ ] **Step 6: Add conservative failure tests**

Cover exact 90/180/270-degree aliases, all square-lattice reflections, integer-pitch ambiguity, equal-count missing-ray-plus-false-detection, low confidence, large residual, and insufficient overlap. Every ambiguous geometry test must assert `CoordinateSystemError` and must not receive a partial mapping.

- [ ] **Step 7: Run matcher tests and commit**

Expected: normal equal-count and reordered cases PASS; `43 -> 27`, `43 -> 39`, and all ambiguous cases reject.

Commit: `feat(m3): match full multispot lattices conservatively`

### Task 4: Calculator and CLI Integration

**Files:**
- Create: `focimeter_system/modules/calibration_calculation/algorithm/input_preparation.py`
- Modify: `focimeter_system/modules/calibration_calculation/algorithm/calculator.py`
- Modify: `focimeter_system/modules/calibration_calculation/algorithm/cli.py`
- Modify: `focimeter_system/modules/calibration_calculation/schemas/result_success.schema.json`
- Modify: `focimeter_system/modules/calibration_calculation/tests/test_calculator.py`
- Modify: `focimeter_system/modules/calibration_calculation/tests/test_algorithm_cli.py`
- Modify: `focimeter_system/modules/calibration_calculation/tests/test_output_contract.py`

**Interfaces:**
- Produces: `prepare_calculation_inputs(calibration, measurement, model) -> PreparedInputs`.
- Preserves: existing `calculate(...)` public signature and v1 behavior.

- [ ] **Step 1: Add failing dispatch and error-envelope tests**

Assert v1 documents bypass experimental matching, experimental documents call the matcher, ambiguous inputs return `COORDINATE_SYSTEM_INVALID`, and `matched_spot_count` rather than raw input count controls the model minimum.

- [ ] **Step 2: Implement schema dispatch**

```python
def prepare_calculation_inputs(calibration, measurement, model):
    versions = {calibration.get("schema_version"), measurement.get("schema_version")}
    if versions == {"1.0"}:
        return PreparedInputs(calibration, measurement, None)
    if versions == {"m2.multispot.experimental.1"}:
        pair = parse_experimental_pair(calibration, measurement)
        return PreparedInputs.from_match(match_experimental_multispot(pair, model.matching_limits))
    raise ModelError("Calibration and measurement schemas must use one supported matching contract.")
```

- [ ] **Step 3: Integrate before existing contract validation**

Call `prepare_calculation_inputs` after model validation but before `validate_inputs`. Pass its paired documents into the existing validator and geometry fit. Map matcher `CoordinateSystemError` to the existing unified envelope.

- [ ] **Step 4: Add optional experimental diagnostics to result schema**

For experimental success include raw counts, unmatched counts, matching RMSE, maximum residual, hypothesis margin, and input schema version. Leave v1 result JSON unchanged.

- [ ] **Step 5: Run calculator, CLI, and output-contract tests**

Expected: existing and new tests PASS.

Commit: `feat(m3): calculate from matched experimental multispot input`

### Task 5: Freeze and Test Reviewed 94-Point M2 Outputs

**Files:**
- Create: `focimeter_system/modules/calibration_calculation/tests/fixtures/m2_experimental_94/spots_calib_multispot.json`
- Create: `focimeter_system/modules/calibration_calculation/tests/fixtures/m2_experimental_94/spots_meas_multispot.json`
- Create: `focimeter_system/modules/calibration_calculation/tests/fixtures/m2_experimental_94/manifest.json`
- Create: `focimeter_system/modules/calibration_calculation/tests/test_m2_experimental_e2e.py`

**Interfaces:**
- Consumes: current M2 CLI output only as fixture-generation input.
- Produces: immutable, hash-checked M3 integration fixtures; tests never invoke M2.

- [ ] **Step 1: Build current M2 without modifying its source**

Use the repository's existing CMake configuration and run the Release CLI with:

```powershell
focimeter_m2.exe --input focimeter_system/data/mock/m2_image_recognition/synthetic_multispot/packages/input_package_94_noisy_gradient.json --output focimeter_system/outputs/m3_fixture_generation_20260804 --project-root focimeter_system --experimental-multispot --save-intermediate
```

Expected: two successful `m2.multispot.experimental.1` JSON files under `experimental_multispot/`.

- [ ] **Step 2: Copy only the two JSON outputs into the M3 fixture directory**

Do not commit M2 build products, logs, or intermediate images.

- [ ] **Step 3: Create a provenance manifest**

Record source commit, exact command arguments, SHA-256 for both files, expected schema, expected counts, `review_status="reviewed_for_software_integration"`, and `metrology_validated=false`.

- [ ] **Step 4: Add end-to-end tests**

Test fixed-fixture provenance, independent detection-array reorder, independent `detection_id` replacement, one-side point removal, and exact-symmetry ambiguity rejection. Assert that the original fixture hashes match the manifest before matching.

- [ ] **Step 5: Run the fixed-fixture tests and commit**

Expected: the reviewed 94-point files keep their provenance hashes but return `COORDINATE_SYSTEM_INVALID`, because their unmarked topology has 180-degree and diagonal-reflection identity aliases. Asymmetric synthetic full-coverage inputs still exercise the successful matching path; no path may pass by copying `detection_id`.

Commit: `test(m3): freeze M2 experimental multispot integration outputs`

### Task 6: Documentation, Full Verification, and PR Update

**Files:**
- Modify: `focimeter_system/modules/calibration_calculation/README.md`
- Modify: `focimeter_system/modules/calibration_calculation/requirements.txt` only if implementation needs an added runtime dependency
- Modify: PR #10 title/body to describe the final M2-to-M3 safety behavior

**Interfaces:**
- Documents both supported input paths and the metrology boundary.

- [ ] **Step 1: Update M3 documentation**

State: “支持 M2 实验多光斑输出，经 M3 保守跨图匹配并生成内部物理光线身份后计算。” Also state that every detected point requires a unique identity, partial overlap and ambiguity are rejected, `detection_id` is never a physical ID, and results remain software/simulation verified.

- [ ] **Step 2: Run focused and full tests**

Run:

```powershell
python -m unittest modules.calibration_calculation.tests.test_experimental_input -v
python -m unittest modules.calibration_calculation.tests.test_multispot_matching -v
python -m unittest modules.calibration_calculation.tests.test_m2_experimental_e2e -v
python -m unittest discover -s modules/calibration_calculation/tests -v
python validate_mock_data.py
```

Expected: all M3 tests and repository JSON validation PASS.

- [ ] **Step 3: Run integration safety checks**

Run M1 tests, `git diff --check`, inspect `git diff origin/develop...HEAD`, and verify no M2 source file is modified by the M3 implementation commits.

- [ ] **Step 4: Commit, push, and update PR #10**

Commit the integration changes, push `task/m2-w0rry-real-jpeg-detection`, and update PR #10 with M2/M3 test counts, four-file fixture provenance, full-coverage rejection behavior, ambiguity rejection, and remaining metrology limitations.
