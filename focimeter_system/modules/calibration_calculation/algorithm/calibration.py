"""Fit and apply standard-lens calibration models."""

from __future__ import annotations

import hashlib
import json
import math
from collections import defaultdict
from pathlib import Path
from typing import Mapping, Sequence

import numpy as np
from jsonschema import Draft202012Validator

from ..validator.contract_validator import validate_inputs
from .geometry import fit_spot_transform
from .power_vector import (
    matrix_to_power_vector,
    power_matrix_from_transform,
    power_vector_to_prescription,
    prescription_to_power_vector,
    transpose_to_minus_cylinder,
)
from .types import CalibrationDataError, CalibrationModel, PowerVector, Prescription


SCHEMA_ROOT = Path(__file__).resolve().parents[1] / "schemas"


def canonical_sha256(data: Mapping[str, object]) -> str:
    encoded = json.dumps(data, ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode("utf-8")
    return hashlib.sha256(encoded).hexdigest()


def fit_linear_correction(
    raw: np.ndarray,
    certified: np.ndarray,
    weights: np.ndarray,
) -> tuple[np.ndarray, np.ndarray]:
    raw = np.asarray(raw, dtype=float)
    certified = np.asarray(certified, dtype=float)
    weights = np.asarray(weights, dtype=float)
    if raw.ndim != 2 or raw.shape[1] != 3 or certified.shape != raw.shape or weights.shape != (len(raw),):
        raise CalibrationDataError("raw and certified must be Nx3 and weights must contain N values.")
    if not np.all(np.isfinite(raw)) or not np.all(np.isfinite(certified)):
        raise CalibrationDataError("Calibration vectors must be finite.")
    if not np.all(np.isfinite(weights)) or np.any(weights <= 0):
        raise CalibrationDataError("Calibration weights must be positive and finite.")
    design = np.column_stack([raw, np.ones(len(raw))])
    if np.linalg.matrix_rank(design) != 4:
        raise CalibrationDataError("Training power vectors must span M, J0, J45, and bias.")
    root_weights = np.sqrt(weights)[:, None]
    coefficients, _, _, _ = np.linalg.lstsq(
        design * root_weights,
        certified * root_weights,
        rcond=None,
    )
    return coefficients[:3].T, coefficients[3]


def apply_correction(vector: PowerVector, model: CalibrationModel) -> PowerVector:
    corrected = model.correction_matrix @ vector.as_array() + model.correction_bias
    if not np.all(np.isfinite(corrected)):
        raise CalibrationDataError("Calibration correction produced non-finite values.")
    return PowerVector(*map(float, corrected))


def _load_schema(name: str) -> dict[str, object]:
    return json.loads((SCHEMA_ROOT / name).read_text(encoding="utf-8"))


def _validate_dataset(dataset: Mapping[str, object]) -> None:
    errors = sorted(
        Draft202012Validator(_load_schema("calibration_dataset.schema.json")).iter_errors(dataset),
        key=lambda error: list(error.absolute_path),
    )
    if errors:
        error = errors[0]
        path = ".".join(map(str, error.absolute_path)) or "$"
        raise CalibrationDataError(f"Invalid calibration dataset at {path}: {error.message}")


def _project_file(project_root: Path, relative_path: str) -> Path:
    root = project_root.resolve()
    path = (root / relative_path).resolve()
    if not path.is_relative_to(root):
        raise CalibrationDataError(f"Calibration path escapes project root: {relative_path}")
    return path


def _load_json(path: Path) -> dict[str, object]:
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError, UnicodeDecodeError) as error:
        raise CalibrationDataError(f"Cannot read calibration JSON {path}: {error}") from error
    if not isinstance(data, dict):
        raise CalibrationDataError(f"Calibration JSON root must be an object: {path}")
    return data


def _certified_prescription(sample: Mapping[str, object]) -> Prescription:
    certified = sample["certified"]
    prescription = Prescription(
        S=float(certified["S"]),
        C=float(certified["C"]),
        A=None if certified["A"] is None else float(certified["A"]),
    )
    notation = certified["notation"]
    if notation == "plus_cylinder" and prescription.C < 0:
        raise CalibrationDataError("plus_cylinder certificate notation requires C >= 0.")
    if notation == "minus_cylinder" and prescription.C > 0:
        raise CalibrationDataError("minus_cylinder certificate notation requires C <= 0.")
    if notation == "plus_cylinder":
        prescription = transpose_to_minus_cylinder(prescription)
    elif prescription.C == 0:
        prescription = Prescription(prescription.S, 0.0, None)
    if prescription.C != 0 and prescription.A is None:
        raise CalibrationDataError("A cylindrical certificate requires an axis.")
    return prescription


def _axis_error(measured: float | None, certified: float | None) -> float:
    if measured is None or certified is None:
        return math.inf
    difference = abs(measured - certified) % 180.0
    return min(difference, 180.0 - difference)


def _validation_metrics(
    predicted: Sequence[Prescription],
    certified: Sequence[Prescription],
    samples: Sequence[Mapping[str, object]],
) -> tuple[dict[str, object], bool]:
    sphere_errors: list[float] = []
    cylinder_errors: list[float] = []
    axis_errors: list[float] = []
    zero_errors: list[float] = []
    vector_errors: list[float] = []
    repeat_groups: dict[tuple[object, ...], list[Prescription]] = defaultdict(list)
    for measured, expected, sample in zip(predicted, certified, samples, strict=True):
        vector_errors.append(
            float(np.linalg.norm(prescription_to_power_vector(measured).as_array() - prescription_to_power_vector(expected).as_array()))
        )
        if expected.C == 0:
            sphere_errors.append(abs(measured.S - expected.S))
            if expected.S == 0:
                zero_errors.append(max(abs(measured.S), abs(measured.C)))
        else:
            cylinder_errors.append(abs(measured.C - expected.C))
            axis_errors.append(_axis_error(measured.A, expected.A))
        key = (sample["serial_number"], expected.S, expected.C, expected.A)
        repeat_groups[key].append(measured)
    repeatability: list[float] = []
    for values in repeat_groups.values():
        if len(values) > 1:
            repeatability.append(max(item.S for item in values) - min(item.S for item in values))
            repeatability.append(max(item.C for item in values) - min(item.C for item in values))

    metrics: dict[str, object] = {
        "validation_sample_count": len(predicted),
        "rmse_power_vector_D": float(np.sqrt(np.mean(np.square(vector_errors)))) if vector_errors else None,
        "max_sphere_error_D": max(sphere_errors, default=None),
        "max_cylinder_error_D": max(cylinder_errors, default=None),
        "max_axis_error_degree": max(axis_errors, default=None),
        "max_zero_error_D": max(zero_errors, default=None),
        "max_repeatability_D": max(repeatability, default=None),
    }
    coverage = all((sphere_errors, cylinder_errors, axis_errors, zero_errors, repeatability))
    passes = coverage and (
        metrics["max_sphere_error_D"] <= 0.06
        and metrics["max_cylinder_error_D"] <= 0.03
        and metrics["max_axis_error_degree"] <= 1.0
        and metrics["max_zero_error_D"] <= 0.03
        and metrics["max_repeatability_D"] <= 0.03
    )
    metrics["jjg_first_class_gates_passed"] = bool(passes)
    metrics["validation_coverage_complete"] = bool(coverage)
    return metrics, bool(passes)


def fit_calibration_model(
    dataset: Mapping[str, object],
    project_root: Path,
    config: Mapping[str, object],
) -> CalibrationModel:
    """Fit a hybrid model from explicit train/validation standard-lens samples."""

    _validate_dataset(dataset)
    try:
        distance_m = float(config["optical"]["distance_m"])
        expected_spot_count = int(config["recognition"]["expected_spot_count"])
        configured_confidence = float(config["recognition"]["min_confidence"])
    except (KeyError, TypeError, ValueError) as error:
        raise CalibrationDataError(f"Invalid fitting configuration: {error}") from error
    if not math.isfinite(distance_m) or distance_m <= 0:
        raise CalibrationDataError("A positive finite distance_m is required.")

    records: list[dict[str, object]] = []
    for sample in dataset["samples"]:
        calibration = _load_json(_project_file(project_root, str(sample["spots_calib_path"])))
        measurement = _load_json(_project_file(project_root, str(sample["spots_meas_path"])))
        report = validate_inputs(calibration, measurement, config, mode="calculation-ready")
        if not report.valid:
            raise CalibrationDataError(f"Sample {sample['sample_id']} failed input validation: {report.to_dict()}")
        geometry = fit_spot_transform(calibration, measurement)
        matrix, skew = power_matrix_from_transform(geometry.transform, distance_m)
        raw = matrix_to_power_vector(matrix)
        certified = _certified_prescription(sample)
        records.append(
            {
                "sample": sample,
                "geometry": geometry,
                "skew": skew,
                "raw": raw,
                "certified": certified,
                "weight": geometry.min_spot_confidence / float(sample["uncertainty_D"]) ** 2,
            }
        )

    training = [record for record in records if record["sample"]["partition"] == "train"]
    validation = [record for record in records if record["sample"]["partition"] == "validation"]
    if not training or not validation:
        raise CalibrationDataError("Both train and validation samples are required.")
    raw_array = np.asarray([record["raw"].as_array() for record in training])
    certified_array = np.asarray(
        [prescription_to_power_vector(record["certified"]).as_array() for record in training]
    )
    weights = np.asarray([record["weight"] for record in training])
    matrix, bias = fit_linear_correction(raw_array, certified_array, weights)

    pseudo_cylinders = []
    for record in records:
        if record["certified"].C == 0:
            corrected = matrix @ record["raw"].as_array() + bias
            pseudo_cylinders.append(2.0 * math.hypot(corrected[1], corrected[2]))
    cylinder_threshold = max(1e-8, max(pseudo_cylinders, default=0.0) + 3.0 * float(np.std(pseudo_cylinders)))

    all_certified = [record["certified"] for record in records]
    sphere_values = [item.S for item in all_certified if item.C == 0]
    if len(sphere_values) < 2:
        raise CalibrationDataError("At least two spherical powers are required.")
    quality_values = {
        "max_fit_rmse_pixel": max(1e-6, 1.5 * max(record["geometry"].rmse_pixel for record in records)),
        "max_condition_number": max(1.0, 1.5 * max(record["geometry"].condition_number for record in records)),
        "max_skew_power_D": max(1e-6, 1.5 * max(float(record["skew"]) for record in records)),
    }

    temporary_dict = {
        "schema_version": "1.0",
        "model_type": "hybrid_power_matrix_v1",
        "model_id": "fitting",
        "validation_status": "simulation_only",
        "source_dataset_sha256": canonical_sha256(dataset),
        "hardware": {"distance_m": distance_m, "expected_spot_count": expected_spot_count},
        "correction": {"matrix": matrix.tolist(), "bias": bias.tolist()},
        "quality_limits": {
            "validated_sphere_range_D": [min(sphere_values), max(sphere_values)],
            "validated_abs_cylinder_max_D": max(abs(item.C) for item in all_certified),
            "cylinder_threshold_D": cylinder_threshold,
            **quality_values,
            "min_confidence": configured_confidence,
            "validation_confidence": 1.0,
        },
        "fit_metrics": {},
        "standard_lenses": [],
    }
    temporary_model = CalibrationModel.from_dict(temporary_dict)
    predicted = [
        power_vector_to_prescription(
            apply_correction(record["raw"], temporary_model),
            cylinder_threshold,
        )
        for record in validation
    ]
    expected = [record["certified"] for record in validation]
    metrics, gates_passed = _validation_metrics(
        predicted,
        expected,
        [record["sample"] for record in validation],
    )
    error_ratios = [
        (metrics["max_sphere_error_D"] or 0.0) / 0.06,
        (metrics["max_cylinder_error_D"] or 0.0) / 0.03,
        (metrics["max_axis_error_degree"] or 0.0) / 1.0,
        (metrics["max_zero_error_D"] or 0.0) / 0.03,
        (metrics["max_repeatability_D"] or 0.0) / 0.03,
    ]
    validation_confidence = max(0.0, min(1.0, 1.0 - max(error_ratios)))
    dataset_hash = canonical_sha256(dataset)
    model_dict = temporary_model.to_dict()
    model_dict["model_id"] = f"{dataset['dataset_id']}-{dataset_hash[:12]}"
    model_dict["validation_status"] = (
        "metrology_validated" if dataset["data_kind"] == "metrology" and gates_passed else "simulation_only"
    )
    model_dict["quality_limits"]["validation_confidence"] = validation_confidence
    model_dict["fit_metrics"] = metrics
    model_dict["standard_lenses"] = [
        {"sample_id": sample["sample_id"], "serial_number": sample["serial_number"]}
        for sample in dataset["samples"]
    ]
    return CalibrationModel.from_dict(model_dict)
