# M3 Real JPEG Safety Integration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Consume M2 PR #10 real JPEG multispot JSON while preserving complete, unique, conservative M3 physical-ray matching and unified error output.

**Architecture:** Extend the existing experimental adapter contract, keep retained-spot quality rejection at the adapter boundary, remove the per-image absolute-orientation gate, and preserve relative transform validation in the matcher. Cross-module tests consume M2-owned JSON directly and exercise both the unsafe-quality and geometry-only rejection paths through the public `calculate` API.

**Tech Stack:** Python 3.11+, NumPy 2.x, jsonschema 4.x, unittest, CMake/CTest for M2 verification, GitHub Actions YAML.

## Global Constraints

- Branch from `origin/task/m2-w0rry-real-jpeg-detection` and target the same branch with the stacked PR.
- Modify M3 consumption, M3 tests, M3 documentation, and M3 CI only; do not modify M2 detection implementation or its sample JSON.
- `detection_id` remains image-local and is never copied or renamed to `spot_id`.
- Any measurement detection without one unique physical identity rejects the whole pair with `COORDINATE_SYSTEM_INVALID`.
- No error response may contain S/C/A or a partial calculation result.
- Preserve the synthetic 94-point fixture and the existing v1 physical-`spot_id` path.

---

### Task 1: Experimental Contract and Quality Boundary

**Files:**
- Modify: `focimeter_system/modules/calibration_calculation/schemas/m2_multispot_experimental.schema.json`
- Modify: `focimeter_system/modules/calibration_calculation/tests/test_experimental_input.py`
- Create: `focimeter_system/modules/calibration_calculation/tests/test_m2_real_jpeg_integration.py`

**Interfaces:**
- Consumes: `parse_experimental_pair(calibration, measurement)`.
- Produces: acceptance of both approved validation scopes and deterministic `CoordinateSystemError` for every non-safe retained-spot flag.

- [ ] **Step 1: Add failing scope and quality tests**

Assert `software_only` parses when point flags are empty, the existing `simulation_only` case still parses, `SATURATED_PEAK` remains safe, and each of the five named unsafe flags raises `CoordinateSystemError`.

- [ ] **Step 2: Run adapter tests and observe failure**

Run: `python -m unittest modules.calibration_calculation.tests.test_experimental_input -v`

Expected: `software_only` fails schema validation before the schema change.

- [ ] **Step 3: Extend validation scope without widening metrology state**

Change the schema property to:

```json
"validation_scope": {"enum": ["simulation_only", "software_only"]}
```

Keep `validation_status` and `metrology_validated` constants unchanged. Keep `IDENTITY_SAFE_QUALITY_FLAGS = frozenset({"SATURATED_PEAK"})` unchanged so all other flags fail closed.

- [ ] **Step 4: Run adapter tests**

Expected: all adapter tests pass.

### Task 2: Absolute Orientation Independence and Complete-Match Diagnostics

**Files:**
- Modify: `focimeter_system/modules/calibration_calculation/algorithm/multispot_matching.py`
- Modify: `focimeter_system/modules/calibration_calculation/tests/test_multispot_matching.py`

**Interfaces:**
- Consumes: `ExperimentalPair` and `MatchingLimits`.
- Produces: per-image lattice assignments at arbitrary installation angles; the existing relative transform gates; explicit incomplete-identity failure.

- [ ] **Step 1: Add failing 42-degree and relative-rotation tests**

Build calibration and measurement lattices with a common 42-degree transform and relative rotations of 5 and 10 degrees; expect complete matching. Use 16 degrees for the failure boundary. Retain reflection and exact 90-degree alias failure tests.

- [ ] **Step 2: Verify the 42-degree success test fails**

Run: `python -m unittest modules.calibration_calculation.tests.test_multispot_matching -v`

Expected: current `_candidate_bases` raises `Lattice orientation exceeds the conservative matching range.`

- [ ] **Step 3: Remove only the absolute-orientation check**

Delete the angle calculation and `max_rotation_degree` check from `_candidate_bases`. Do not change the SVD-derived relative rotation check in `_fit_candidate`.

- [ ] **Step 4: Make incomplete identity explicit before scoring**

After enumerating topology hypotheses, compute `maximum_topological_overlap`. When it is below `len(measurement_points)`, raise:

```python
raise CoordinateSystemError(
    f"Cross-image matching assigned {maximum_topological_overlap} of "
    f"{len(measurement_points)} measurement detections; every measurement "
    "detection requires one unique physical identity."
)
```

Do not pass a 23-point subset into `_fit_candidate` or `fit_spot_transform`.

- [ ] **Step 5: Run matcher tests**

Expected: common 42-degree inputs pass; 16-degree relative rotation, reflection, 90-degree alias, and incomplete mappings fail.

### Task 3: Real M2 JSON Safety Integration

**Files:**
- Create: `focimeter_system/modules/calibration_calculation/tests/test_m2_real_jpeg_integration.py`
- Modify: `focimeter_system/modules/calibration_calculation/README.md`

**Interfaces:**
- Consumes: the four M2-owned JSON files through public `calculate(...)` and internal parsing only where matched-count diagnostics must be isolated.
- Produces: stable `COORDINATE_SYSTEM_INVALID` results with no prescription fields for current real samples.

- [ ] **Step 1: Load M2 samples by repository path**

Define `REAL_SAMPLE_ROOT = ROOT / "modules/image_recognition/samples/real_jpeg_software_verified"` and load both pairs without copying fixtures into M3.

- [ ] **Step 2: Test original pair rejection**

For pair 1 and pair 2, call `calculate(..., allow_simulation_model=True)` and assert:

```python
self.assertEqual("COORDINATE_SYSTEM_INVALID", result["error"]["code"])
self.assertNotIn("result", result)
self.assertNotIn("lens_type", result)
```

- [ ] **Step 3: Test pair 2 geometry-only isolation**

Deep-copy both documents, clear every `spots[].quality_flags` and `quality.warnings`, call `calculate`, and assert the error message contains `23 of 27`, the code remains `COORDINATE_SYSTEM_INVALID`, and no prescription fields exist.

- [ ] **Step 4: Document the exact safety boundary**

State that M3 accepts the PR #10 metadata envelope but intentionally rejects the current real pairs: first on unsafe retained-spot flags and, after isolated flag removal, on incomplete 23/27 physical identity. Do not claim real-image matching success or metrology validation.

### Task 4: Stacked-PR CI and Full Verification

**Files:**
- Modify: `.github/workflows/m3-ci.yml`

**Interfaces:**
- Consumes: stacked PR events and M2 real JSON paths.
- Produces: automated M3 regression on the PR #10 target branch.

- [ ] **Step 1: Extend workflow target and path filters**

Add `task/m2-w0rry-real-jpeg-detection` under `pull_request.branches` and add:

```yaml
- "focimeter_system/modules/image_recognition/samples/real_jpeg_software_verified/**"
```

- [ ] **Step 2: Run all required verification**

Run:

```powershell
python -m unittest discover -s modules/calibration_calculation/tests -v
python validate_mock_data.py
cmake --build <m2-build-dir> --config Release
ctest --test-dir <m2-build-dir> -C Release --output-on-failure
git diff --check
```

Expected: M3, M2, mock validation, and whitespace checks all pass.

- [ ] **Step 3: Commit and push**

Commit only M3/CI/spec/plan files with message `fix(m3): reject unsafe real multispot identities`, push `task/m3-gdfzs-real-jpeg-safety-integration`, and verify the remote head.

- [ ] **Step 4: Create stacked PR**

Create a PR with head `task/m3-gdfzs-real-jpeg-safety-integration` and base `task/m2-w0rry-real-jpeg-detection`. Report commit SHA, `COORDINATE_SYSTEM_INVALID`, raw unsafe-flag rejection, isolated `23/27` rejection, and every test count.
