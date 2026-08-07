# M3 Real-Data Engineering Mode

M3 keeps formal calculation as the default. Real M2 experimental multispot
documents can produce a software-only engineering result only when the caller
passes `engineering_mode=True` or the CLI flag `--engineering-mode`.

```powershell
python -m modules.calibration_calculation.algorithm.cli calculate `
  --calibration <spots_calib_multispot.json> `
  --measurement <spots_meas_multispot.json> `
  --config config/default_config.json `
  --model modules/calibration_calculation/examples/calibration/calibration_model.simulation.json `
  --engineering-mode
```

Engineering mode does not turn `detection_id` into `spot_id`. M3 still requires
all measurement detections to receive one unique cross-image identity and keeps
the existing rejection rules for unmatched detections, ambiguous hypotheses,
reflections, 90-degree aliases, and a matched confidence product below `0.35`.
No prescription is calculated from a matching subset.

The current real fixture markers reviewed for engineering use are preserved in
the output `quality.warnings`; unknown or identity-unsafe spot flags are still
rejected. Formal residual, skew-power, and calibrated-range gates that depend on
unfinished hardware calibration are reported as explicit warnings when they are
not applied.

Camera power matrices are converted to instrument coordinates through one
proper-rotation basis mapping shared by every engineering input. The mapping is
not selected by filename, point count, coordinates, hash, or reference S/C/A.
Engineering mode also uses a general `0.06 D` cylinder-zero threshold, so a
sub-threshold cylinder is returned as `C=0` and `A=null`.

Every engineering result states:

```json
{
  "validation_status": "software_verified",
  "validation_scope": "software_only",
  "metrology_validated": false
}
```

These values mean that the software path ran successfully. They do not claim
metrology validation, hardware accuracy, or regulatory conformance.
