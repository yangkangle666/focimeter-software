"""Online M3 calculation orchestration."""

from __future__ import annotations

import math
from typing import Mapping

from ..validator.contract_validator import validate_inputs
from .calibration import apply_correction
from .geometry import fit_spot_transform
from .power_vector import matrix_to_power_vector, power_matrix_from_transform, power_vector_to_prescription
from .types import (
    CalculationError,
    CalibrationModel,
    CoordinateSystemError,
    ModelError,
)


MODULE_NAME = "m3_calibration_calculation"


def _error(
    task_id: str,
    code: str,
    message: str,
    recoverable: bool,
    reason: str,
    **details: object,
) -> dict[str, object]:
    return {
        "schema_version": "1.0",
        "task_id": task_id,
        "module": MODULE_NAME,
        "status": "error",
        "error": {
            "code": code,
            "message": message,
            "module": MODULE_NAME,
            "recoverable": recoverable,
            "details": {"reason": reason, **details},
        },
    }


def _quality_score(value: float, limit: float) -> float:
    ratio = min(1.0, max(0.0, value / limit))
    return 1.0 - 0.25 * ratio


def calculate(
    calibration: Mapping[str, object],
    measurement: Mapping[str, object],
    config: Mapping[str, object],
    model: CalibrationModel | Mapping[str, object],
    allow_simulation_model: bool = False,
) -> dict[str, object]:
    """Calculate an M3 result or return the existing unified error envelope."""

    task_id = str(calibration.get("task_id", measurement.get("task_id", "unknown_task")))
    try:
        parsed_model = model if isinstance(model, CalibrationModel) else CalibrationModel.from_dict(model)
        if parsed_model.validation_status != "metrology_validated" and not allow_simulation_model:
            raise ModelError("Production calculation requires a metrology-validated algorithm version.")

        report = validate_inputs(calibration, measurement, config, mode="calculation-ready")
        if not report.valid:
            issue = report.issues[0]
            return _error(
                task_id,
                issue.code,
                issue.message,
                True,
                "INPUT_CONTRACT_INVALID",
                path=issue.path,
            )

        distance_m = float(config["optical"]["distance_m"])
        expected_count = config["recognition"]["expected_spot_count"]
        if not math.isclose(distance_m, parsed_model.distance_m, rel_tol=1e-12, abs_tol=1e-15):
            raise ModelError("Configuration distance_m does not match the calibration artifact hardware fingerprint.")
        if isinstance(expected_count, int) and expected_count != parsed_model.expected_spot_count:
            raise ModelError("Configured expected_spot_count does not match the calibration artifact hardware fingerprint.")
        if len(calibration["spots"]) < parsed_model.expected_spot_count:
            raise ModelError("Matched spot count is below the calibration artifact minimum.")

        geometry = fit_spot_transform(calibration, measurement)
        limits = parsed_model.quality_limits
        if geometry.condition_number > limits.max_condition_number:
            raise CalculationError("GEOMETRY_CONDITION_EXCEEDED")
        if geometry.rmse_pixel > limits.max_fit_rmse_pixel:
            raise CalculationError("FIT_RESIDUAL_EXCEEDED")
        required_confidence = max(float(config["recognition"]["min_confidence"]), limits.min_confidence)
        if geometry.min_spot_confidence < required_confidence:
            raise CalculationError("SPOT_CONFIDENCE_TOO_LOW")

        power_matrix, skew_power = power_matrix_from_transform(geometry.transform, distance_m)
        if skew_power > limits.max_skew_power_D:
            raise CalculationError("SKEW_POWER_EXCEEDED")
        corrected = apply_correction(matrix_to_power_vector(power_matrix), parsed_model)
        prescription = power_vector_to_prescription(corrected, limits.cylinder_threshold_D)

        low, high = limits.validated_sphere_range_D
        principal_powers = (prescription.S, prescription.S + prescription.C)
        if (
            any(power < low or power > high for power in principal_powers)
            or abs(prescription.C) > limits.validated_abs_cylinder_max_D
        ):
            return _error(
                task_id,
                "CALCULATION_FAILED",
                "Calculated power is outside the algorithm version's validated range.",
                False,
                "RESULT_OUTSIDE_VALIDATED_RANGE",
                principal_powers_D=list(principal_powers),
            )

        confidence = min(
            geometry.min_spot_confidence,
            limits.validation_confidence,
            _quality_score(geometry.rmse_pixel, limits.max_fit_rmse_pixel),
            _quality_score(geometry.condition_number, limits.max_condition_number),
            _quality_score(skew_power, limits.max_skew_power_D),
        )
        if confidence < required_confidence:
            raise CalculationError("COMBINED_CONFIDENCE_TOO_LOW")

        warnings = []
        input_warnings = {
            str(warning)
            for document in (calibration, measurement)
            for warning in document["quality"].get("warnings", [])
        }
        if "MOCK_DATA_ONLY" in input_warnings:
            warnings.append("MOCK_DATA_ONLY")
            warnings.append("software_verified")
        if parsed_model.validation_status == "simulation_only":
            warnings.append("simulation_model_allowed_for_test_only")
        intermediate: dict[str, object] = {
            "coordinate_system_valid": True,
            "coordinate_type_before": "image_pixel",
            "coordinate_type_after": "calibration_pixel",
            "shift_unit": "pixel",
        }
        if "y_positive" in geometry.shifts:
            dx, dy = geometry.shifts["y_positive"]
            intermediate["shift_y_positive"] = {"dx": dx, "dy": dy}
        if "x_positive" in geometry.shifts:
            dx, dy = geometry.shifts["x_positive"]
            intermediate["shift_x_positive"] = {"dx": dx, "dy": dy}

        lens_type = "spherical" if prescription.C == 0 else "cylindrical"
        return {
            "schema_version": "1.0",
            "task_id": task_id,
            "module": MODULE_NAME,
            "status": "ok",
            "lens_type": lens_type,
            "result": {
                "S": prescription.S,
                "C": prescription.C,
                "A": prescription.A,
                "unit": "D",
                "angle_unit": "degree",
            },
            "quality": {
                "is_usable": True,
                "confidence": confidence,
                "validation_status": (
                    "software_verified"
                    if "software_verified" in warnings
                    else parsed_model.validation_status
                ),
                "matched_spot_count": len(calibration["spots"]),
                "fit_rmse": geometry.rmse_pixel,
                "condition_number": geometry.condition_number,
                "warnings": warnings,
            },
            "intermediate": intermediate,
            "error": None,
        }
    except CoordinateSystemError as error:
        return _error(task_id, "COORDINATE_SYSTEM_INVALID", str(error), True, "GEOMETRY_INVALID")
    except ModelError as error:
        return _error(task_id, "CONFIG_INVALID", str(error), False, "MODEL_INVALID")
    except CalculationError as error:
        return _error(task_id, "CALCULATION_FAILED", str(error), True, str(error))
    except Exception as error:
        return _error(task_id, "UNKNOWN_ERROR", str(error), False, "UNEXPECTED_EXCEPTION")
