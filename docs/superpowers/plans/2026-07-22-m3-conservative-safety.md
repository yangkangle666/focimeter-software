# M3 Conservative Safety Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make M3 reject unreliable correspondence and preserve independent calibration, validation, and final test sets without changing shared M2/M3 fields.

**Architecture:** M3 pairs spots by unique semantic `role`, uses the C++ Y-first basis, and rejects reflected or direction-reversing transforms. The legacy `train` partition is the calibration set, validation records define the proposed operating range, and an independent final test set evaluates the frozen algorithm version.

**Tech Stack:** Python 3.11, NumPy, jsonschema, unittest.

## Global Constraints

- Modify runtime code only in `focimeter_system/modules/calibration_calculation/`.
- Preserve shared M2/M3 JSON field names and keep `distance_m` configuration-driven.
- Do not add `ray_id` without project-lead approval.
- Run M3 unittest discovery, `validate_mock_data.py`, and `git diff --check` before final commit.

---

### Task 1: Safe role pairing

**Files:**
- Modify: `focimeter_system/modules/calibration_calculation/validator/contract_validator.py`
- Modify: `focimeter_system/modules/calibration_calculation/algorithm/geometry.py`
- Test: `focimeter_system/modules/calibration_calculation/tests/test_input_contract.py`
- Test: `focimeter_system/modules/calibration_calculation/tests/test_geometry.py`

**Interfaces:** `validate_inputs(..., mode="calculation-ready")` rejects duplicate or `unknown` roles. `fit_spot_transform` pairs each unique role and raises `CoordinateSystemError` for reflected or reversed geometry.

- [ ] Write failing tests: an `unknown` role and a duplicate `left_or_negative` role return `COORDINATE_SYSTEM_INVALID`; permuted measurement `spot_id` values with unchanged roles still recover identity; a `diag(-1, 1)` transform is rejected.
- [ ] Run `python -m unittest modules.calibration_calculation.tests.test_input_contract modules.calibration_calculation.tests.test_geometry -v`; verify the new tests fail.
- [ ] Add `_pairing_role_issues` to reject `unknown` and duplicate roles only in calculation-ready mode.
- [ ] Replace ID maps in `fit_spot_transform` with role maps and add:

```python
if np.linalg.det(transform) <= 0:
    raise CoordinateSystemError("Spot transform reverses orientation.")
if any(float(np.dot(source, target)) <= 0 for source, target in zip(x, y, strict=True)):
    raise CoordinateSystemError("A paired outer spot reverses direction.")
```

- [ ] Rerun the focused tests; commit with `fix: reject unreliable M3 spot pairing`.

### Task 2: C++-aligned geometry and shifts

**Files:**
- Modify: `focimeter_system/modules/calibration_calculation/algorithm/geometry.py`
- Test: `focimeter_system/modules/calibration_calculation/tests/test_geometry.py`
- Test: `focimeter_system/modules/calibration_calculation/tests/test_calculator.py`

**Interfaces:** `_orthonormal_basis` returns the C++-compatible Y-first basis. `GeometryFit.shifts` remains `Mapping[str, tuple[float, float]]` but values are `calibration_pixel` coordinates.

- [ ] Write a failing non-orthogonal-axis test that asserts an identity transform using `y_positive` as the basis anchor, and a shift test that proves `(3, 4)` image pixels are projected before output.
- [ ] Run `python -m unittest modules.calibration_calculation.tests.test_geometry -v`; verify the shift test fails.
- [ ] Replace the X-first Gram-Schmidt construction with:

```python
ey = y_hint / np.linalg.norm(y_hint)
ex = np.asarray([-ey[1], ey[0]], dtype=float)
if abs(float(np.dot(x_hint, ex))) <= np.finfo(float).eps * scale * 100:
    raise CoordinateSystemError("x_positive cannot validate the calibration X axis.")
if np.dot(x_hint, ex) < 0:
    ex = -ex
basis = np.column_stack([ex, ey])
```

- [ ] Project shift output with `basis.T @ (meas_vector - calib_vector)`.
- [ ] Run `python -m unittest modules.calibration_calculation.tests.test_geometry modules.calibration_calculation.tests.test_calculator -v`; commit with `fix: align M3 geometry with reference basis`.

### Task 3: Independent metrology partitions

**Files:**
- Modify: `focimeter_system/modules/calibration_calculation/algorithm/calibration.py`
- Test: `focimeter_system/modules/calibration_calculation/tests/test_algorithm_cli.py`
- Test: `focimeter_system/modules/calibration_calculation/tests/test_calibration_algorithm.py`

**Interfaces:** `fit_calibration_model(dataset, project_root, config)` rejects duplicate sample IDs and serial-number, path, or measurement-content reuse across partitions. Its correction, cylinder threshold, and geometry limits use only calibration records; exported range metadata uses validation records; final gate metrics use test records.

- [ ] Write failing synthetic-dataset tests for duplicate `sample_id`, cross-partition serial/path/content reuse, and validation-only range metadata `[0.0, 2.5]` when the calibration set spans `[-5.0, 5.0]`.
- [ ] Run `python -m unittest modules.calibration_calculation.tests.test_algorithm_cli modules.calibration_calculation.tests.test_calibration_algorithm -v`; verify failures.
- [ ] Store each parsed measurement in its record; reject duplicate IDs and serial/path/content leakage across calibration, validation, and test partitions.
- [ ] Derive `pseudo_cylinders` and `quality_values` from the calibration set; derive the validated range from validation; evaluate final gates only on test records.
- [ ] Rerun focused calibration tests; commit with `fix: isolate M3 calibration validation`.

### Task 4: Documentation and complete verification

**Files:**
- Modify: `focimeter_system/modules/calibration_calculation/README.md`

**Interfaces:** README states that role pairing tolerates ID renumbering but is not physical-ray tracking; real-device use requires M2 `ray_id` or an approved matching protocol plus standard-lens validation.

- [ ] Update the capability and limitation sections without changing interface terminology.
- [ ] Run `python -m unittest discover -s modules/calibration_calculation/tests -v` from `focimeter_system`; expect all tests to pass.
- [ ] Run `python validate_mock_data.py` from `focimeter_system`; expect `OK: validated 16 JSON files`.
- [ ] Run `git diff --check` and `git diff --name-only origin/feature/m3-calc...HEAD`; expect no whitespace errors and only M3 files plus approved design/plan documents.
- [ ] Commit with `docs: clarify M3 correspondence limits`.

## Self-Review

- Spec coverage: Tasks 1-2 cover safe correspondence, C++ basis, and calibrated shifts. Task 3 removes validation leakage. Task 4 documents the remaining M2 dependency and verifies the branch.
- Placeholder scan: no deferred behavior or unresolved implementation markers are present.
- Type consistency: public signatures and `GeometryFit.shifts` type remain unchanged.
