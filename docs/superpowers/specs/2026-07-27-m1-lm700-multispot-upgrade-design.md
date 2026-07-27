# M1 LM700 / Hartmann Multi-Spot Upgrade Design

Date: 2026-07-27

Module: M1 input and configuration

Branch: `task/m1-snoopy-ui-fl800-profile`

## 1. Context

The project direction has changed from a fixed five-spot demonstration to the LM700 / Hartmann multi-spot measurement pipeline. M1 remains responsible only for selecting input images, validating configuration and referenced files, and producing a portable input package for downstream modules.

The current M1 implementation still presents `expected_spot_count=5` as the default and promotes a five-spot task on the first screen. It also lacks explicit data provenance, validation level, hardware confirmation state, and a checked calibration file reference.

This upgrade keeps the public M1 result envelope and `schema_version=1.0` unchanged while making the default workflow suitable for multi-spot software integration. The five-spot workflow remains available only as a legacy compatibility fixture.

## 2. Goals

- Make automatic multi-spot detection the default downstream expectation.
- Provide a deterministic synthetic multi-spot reference/measurement image pair for one-click integration testing.
- Record whether input data is `synthetic`, `mock`, or `real`.
- Record whether the result is `simulation_only`, `software_verified`, or `metrology_validated`.
- Record whether hardware parameters have been confirmed.
- Validate a project-relative calibration file and include it in the M1-to-M2 bundle.
- Keep the existing M1 output envelope and error shape stable.
- Preserve old configuration files and the five-spot fixture as compatibility paths.
- Make the web interface explain the current data and validation state in Chinese.

## 3. Non-Goals

- M1 will not detect, count, identify, or match spots.
- M1 will not calculate displacement, wavefront, S/C/A, prism, addition, or UV values.
- M1 will not implement MATLAB, LM700 APK, M2, or M3 algorithms.
- M1 will not claim real hardware accuracy or metrology validity.
- This change will not rename public envelope fields or change `schema_version`.
- This change will not connect to a live camera.

## 4. Compatibility Boundary

The following public result shape remains unchanged:

```json
{
  "schema_version": "1.0",
  "task_id": "...",
  "module": "m1_input_config",
  "status": "ok",
  "data": {},
  "quality": {},
  "error": null
}
```

The existing `data.calibration_image`, `data.measurement_image`, `data.config_path`, and `data.run_mode` fields remain unchanged. New provenance and calibration information is carried by the referenced configuration and by additive warning strings. This avoids forcing an uncoordinated public envelope migration on M2.

## 5. Configuration Design

### 5.1 Recognition mode

The default recognition section becomes:

```json
{
  "spot_count_mode": "auto",
  "expected_spot_count": null,
  "min_confidence": 0.7
}
```

Rules:

- `spot_count_mode=auto` means M1 does not prescribe a spot count.
- `expected_spot_count=null` is required for the default multi-spot profile.
- `spot_count_mode=fixed` requires a positive integer `expected_spot_count` and is reserved for compatibility fixtures.
- The legacy five-spot configuration uses `fixed` and `5`.

### 5.2 Data profile

The configuration adds:

```json
{
  "data_profile": {
    "data_source": "synthetic",
    "validation_status": "simulation_only",
    "hardware_parameters_confirmed": false
  }
}
```

Allowed values and combinations:

| Data source | Allowed validation status | Hardware confirmed |
| --- | --- | --- |
| `synthetic` | `simulation_only` | `false` |
| `mock` | `simulation_only` or `software_verified` | `false` |
| `real` | `software_verified` | `false` or `true` |
| `real` | `metrology_validated` | `true` only |

`metrology_validated` is rejected unless the data source is real, hardware parameters are confirmed, and the calibration file also declares a metrology-validated status.

### 5.3 Calibration reference

The configuration adds:

```json
{
  "calibration_reference": {
    "calibration_file": "data/calibration/simulation_calibration.json",
    "calibration_version": "simulation-v1",
    "parameter_status": "simulated"
  }
}
```

The path must be relative to the project root. M1 resolves the file, verifies that it exists, parses its JSON, and checks that its version and status agree with the main configuration.

### 5.4 Simulation calibration file

`data/calibration/simulation_calibration.json` contains only replaceable simulation parameters:

```json
{
  "schema_version": "1.0",
  "calibration_version": "simulation-v1",
  "parameter_status": "simulated",
  "validation_status": "simulation_only",
  "hardware_parameters_confirmed": false,
  "parameters": {
    "pixel_pitch_mm": 0.0048,
    "effective_focal_length_mm": 12.0,
    "distance_m": 0.03,
    "hartmann_spacing_mm": null,
    "optical_magnification": null,
    "power_sign": -1.0,
    "wavelength_nm": null
  }
}
```

These values are explicitly simulation defaults based on the current project configuration and the teacher-provided MATLAB research defaults. They are not LM700 factory values. Unknown parameters remain `null` and generate pending warnings.

## 6. Input Profiles

### 6.1 Default multi-spot simulation profile

The first-screen primary action becomes `LM700 / Hartmann 多光斑模拟联调`.

It selects:

- `data/synthetic/generated_images/hartmann_reference.png`
- `data/synthetic/generated_images/hartmann_measurement.png`
- `config/default_config.json`
- `data/calibration/simulation_calibration.json`
- `data_source=synthetic`
- `validation_status=simulation_only`

The synthetic images contain a deterministic dense Hartmann-style field with spatially varying displacement. Their purpose is to exercise file handling and downstream multi-spot interfaces. M1 does not inspect or assert their spot count.

### 6.2 Legacy five-spot profile

The existing five-spot images and configuration remain available in a secondary `历史兼容测试` area. Selecting it sets `spot_count_mode=fixed`, `expected_spot_count=5`, and `data_source=mock`.

Every successful legacy run includes:

```text
LEGACY_FIVE_SPOT_COMPATIBILITY: 仅用于旧接口兼容测试，不是 LM700 / Hartmann 正式算法目标。
```

### 6.3 User-selected and uploaded files

Users can continue selecting or uploading reference images, measurement images, and configuration JSON. Step 4 exposes the data source, validation status, hardware confirmation, recognition mode, and calibration reference using Chinese labels while retaining the raw keys.

## 7. Validation and Data Flow

```text
Web or CLI request
  -> validate request envelope
  -> resolve reference image
  -> resolve measurement image
  -> resolve and parse main configuration
  -> validate recognition and data profile combinations
  -> resolve and parse referenced calibration file
  -> compare calibration version and status
  -> generate input_package.json and log
  -> build portable ZIP with every referenced file
```

For new multi-spot profiles, `paths_checked`, `config_checked`, and `is_usable` become true only after all four referenced files have passed their checks. Backward-compatible configurations without `calibration_reference` remain readable; for those files the flags cover every dependency they actually declare, and warnings identify the missing provenance and calibration metadata.

## 8. Warning Policy

Successful results add machine-readable warning prefixes:

```text
DATA_SOURCE: synthetic
VALIDATION_STATUS: simulation_only
HARDWARE_PARAMETERS_UNCONFIRMED: calibration.parameters
CONFIG_PARAMETER_PENDING: calibration.parameters.hartmann_spacing_mm
CONFIG_PARAMETER_PENDING: calibration.parameters.optical_magnification
CONFIG_PARAMETER_PENDING: calibration.parameters.wavelength_nm
SOFTWARE_INTEGRATION_ONLY: ...
```

Warnings never upgrade a result to metrology validity. Real image paths alone do not imply real calibration or validated accuracy.

## 9. Error Handling

- A missing calibration file returns the existing `CONFIG_NOT_FOUND` code with `missing_field=calibration_file`.
- Invalid calibration JSON, version mismatch, status mismatch, or an impossible validation combination returns `CONFIG_INVALID`.
- A missing image continues to return `IMAGE_NOT_FOUND`.
- An invalid or absolute project path continues to be rejected.
- Failed runs retain the standard M1 error envelope and log behavior.
- M1 never fabricates a successful path check after a missing dependency.

No new public error code is introduced in this upgrade.

## 10. Integration Bundle

The ZIP bundle contains:

```text
input_package.json
README_M1_M2_INTEGRATION.md
<reference image>
<measurement image>
<main configuration>
<referenced calibration file>
```

The archive preserves project-relative paths. The README states the data source, validation status, calibration version, and that the package is not evidence of real metrology validation.

## 11. Web Interface Changes

- Replace the five-spot primary banner with a restrained multi-spot simulation banner.
- Show persistent badges for data source, validation status, and hardware confirmation.
- Keep the six-step workflow and existing upload behavior.
- Add Chinese controls for automatic/fixed spot count and calibration metadata.
- Move the five-spot action into a clearly labeled compatibility area.
- On the result screen, distinguish simulation, software verification, and metrology validation states.
- Keep all warnings visible and preserve raw JSON/log tabs.

The interface will not display claims such as `精度合格` or `正式检测可用` while using simulated or unconfirmed parameters.

## 12. Tests

Python contract tests cover:

- default configuration uses automatic multi-spot mode;
- synthetic data profile and simulation calibration are accepted;
- missing calibration file fails before package generation;
- invalid validation/hardware combinations are rejected;
- calibration version mismatch is rejected;
- legacy fixed five-spot configuration remains accepted with a compatibility warning;
- old configuration files without new optional sections remain readable;
- package quality flags are true only after real file checks.

Web and bundle tests cover:

- the primary multi-spot entry and secondary legacy entry;
- Chinese labels for new configuration fields;
- result-state badges and warnings;
- the generated ZIP includes the calibration file;
- a missing referenced calibration file blocks ZIP generation.

Final verification includes the full M1 unit suite, mock JSON validation, JavaScript syntax check, diff check, a live `localhost:8765` run, and ZIP content inspection.

## 13. Module Impact

- M1 changes configuration validation, package construction, bundle construction, fixtures, tests, documentation, and web presentation.
- M2 and M3 code are not modified.
- M2 is expected to interpret `spot_count_mode=auto` and `expected_spot_count=null` when it adopts the new shared configuration.
- The five-spot fixture remains available so existing first-stage consumers can continue testing during migration.
- The pull request description must state that the modification upgrades the fixed five-spot interface toward the LM700 / Hartmann multi-spot direction.

## 14. Delivery

Implementation stays on the current task branch. It is tested locally, then may be pushed and submitted by pull request only after the user explicitly requests publication. It is not merged directly into `develop` or `main`.
