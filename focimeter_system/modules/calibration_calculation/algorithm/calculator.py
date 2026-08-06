"""Online M3 calculation orchestration."""

from __future__ import annotations

import math
from typing import Mapping

import numpy as np

from ..validator.contract_validator import validate_inputs
from .calibration import apply_correction
from .geometry import fit_spot_transform
from .input_preparation import prepare_calculation_inputs
from .power_vector import (
    map_power_matrix_between_bases,
    matrix_to_power_vector,
    power_matrix_from_transform,
    power_vector_to_prescription,
)
from .types import (
    CalculationError,
    CalibrationModel,
    CoordinateSystemError,
    ModelError,
)


MODULE_NAME = "m3_calibration_calculation"
ENGINEERING_CYLINDER_THRESHOLD_D = 0.06
# Instrument X points along camera +Y and instrument Y along camera -X in the
# current M3 engineering coordinate convention. This proper rotation applies
# uniformly to every input; a reflection is deliberately not permitted.
CAMERA_FROM_INSTRUMENT_BASIS = np.asarray([[0.0, -1.0], [1.0, 0.0]])


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
    engineering_mode: bool = False,
) -> dict[str, object]:
    """Calculate an M3 result or return the existing unified error envelope."""

    task_id = str(calibration.get("task_id", measurement.get("task_id", "unknown_task")))
    try:
        parsed_model = model if isinstance(model, CalibrationModel) else CalibrationModel.from_dict(model)
        if (
            parsed_model.validation_status != "metrology_validated"
            and not allow_simulation_model
            and not engineering_mode
        ):
            raise ModelError("Production calculation requires a metrology-validated algorithm version.")

        prepared = prepare_calculation_inputs(
            calibration,
            measurement,
            parsed_model,
            engineering_mode=engineering_mode,
        )
        if engineering_mode and prepared.matching is None:
            raise ModelError("Engineering mode requires M2 experimental multispot inputs.")
        paired_calibration = prepared.calibration
        paired_measurement = prepared.measurement

        report = validate_inputs(paired_calibration, paired_measurement, config, mode="calculation-ready")
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
        if len(paired_calibration["spots"]) < parsed_model.expected_spot_count:
            raise ModelError("Matched spot count is below the calibration artifact minimum.")

        geometry = fit_spot_transform(paired_calibration, paired_measurement)
        limits = parsed_model.quality_limits
        if geometry.condition_number > limits.max_condition_number:
            raise CalculationError("GEOMETRY_CONDITION_EXCEEDED")
        if not engineering_mode and geometry.rmse_pixel > limits.max_fit_rmse_pixel:
            raise CalculationError("FIT_RESIDUAL_EXCEEDED")
        formal_required_confidence = max(
            float(config["recognition"]["min_confidence"]),
            limits.min_confidence,
        )
        if not engineering_mode and geometry.min_spot_confidence < formal_required_confidence:
            raise CalculationError("SPOT_CONFIDENCE_TOO_LOW")

        power_matrix, skew_power = power_matrix_from_transform(geometry.transform, distance_m)
        if not engineering_mode and skew_power > limits.max_skew_power_D:
            raise CalculationError("SKEW_POWER_EXCEEDED")
        if engineering_mode:
            power_matrix = map_power_matrix_between_bases(
                power_matrix,
                CAMERA_FROM_INSTRUMENT_BASIS,
            )
        corrected = apply_correction(matrix_to_power_vector(power_matrix), parsed_model)
        cylinder_threshold = (
            ENGINEERING_CYLINDER_THRESHOLD_D
            if engineering_mode
            else limits.cylinder_threshold_D
        )
        prescription = power_vector_to_prescription(corrected, cylinder_threshold)

        low, high = limits.validated_sphere_range_D
        principal_powers = (prescription.S, prescription.S + prescription.C)
        outside_validated_range = (
            any(power < low or power > high for power in principal_powers)
            or abs(prescription.C) > limits.validated_abs_cylinder_max_D
        )
        if outside_validated_range and not engineering_mode:
            return _error(
                task_id,
                "CALCULATION_FAILED",
                "Calculated power is outside the algorithm version's validated range.",
                False,
                "RESULT_OUTSIDE_VALIDATED_RANGE",
                principal_powers_D=list(principal_powers),
            )

        if engineering_mode:
            assert prepared.matching is not None
            confidence = min(
                prepared.matching.min_pair_confidence_product,
                limits.validation_confidence,
                _quality_score(geometry.condition_number, limits.max_condition_number),
            )
            if confidence < parsed_model.matching_limits.min_confidence:
                raise CalculationError("COMBINED_CONFIDENCE_TOO_LOW")
        else:
            confidence = min(
                geometry.min_spot_confidence,
                limits.validation_confidence,
                _quality_score(geometry.rmse_pixel, limits.max_fit_rmse_pixel),
                _quality_score(geometry.condition_number, limits.max_condition_number),
                _quality_score(skew_power, limits.max_skew_power_D),
            )
            if confidence < formal_required_confidence:
                raise CalculationError("COMBINED_CONFIDENCE_TOO_LOW")

        warnings = list(prepared.source_markers)
        input_warnings = {
            str(warning)
            for document in (paired_calibration, paired_measurement)
            for warning in document["quality"].get("warnings", [])
        }
        if "MOCK_DATA_ONLY" in input_warnings:
            warnings.append("MOCK_DATA_ONLY")
            warnings.append("software_verified")
        if parsed_model.validation_status == "simulation_only":
            warnings.append(
                "simulation_model_allowed_for_engineering_only"
                if engineering_mode
                else "simulation_model_allowed_for_test_only"
            )
        if prepared.matching is not None:
            warnings.append("M2_EXPERIMENTAL_MULTISPOT")
            if "software_verified" not in warnings:
                warnings.append("software_verified")
        if engineering_mode:
            warnings.extend([
                "ENGINEERING_MODE",
                "software_only",
                "NOT_METROLOGY_VALIDATED",
                "CAMERA_TO_INSTRUMENT_BASIS_MAPPING_APPLIED",
                "ENGINEERING_CYLINDER_ZERO_THRESHOLD_0.06_D",
            ])
            if geometry.rmse_pixel > limits.max_fit_rmse_pixel:
                warnings.append("FORMAL_FIT_RESIDUAL_GATE_NOT_APPLIED")
            if skew_power > limits.max_skew_power_D:
                warnings.append("FORMAL_SKEW_GATE_NOT_APPLIED")
            if outside_validated_range:
                warnings.append("FORMAL_VALIDATED_RANGE_GATE_NOT_APPLIED")
        warnings = list(dict.fromkeys(warnings))
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
        result: dict[str, object] = {
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
                **(
                    {
                        "validation_scope": "software_only",
                        "metrology_validated": False,
                    }
                    if engineering_mode
                    else {}
                ),
                "matched_spot_count": len(paired_calibration["spots"]),
                "fit_rmse": geometry.rmse_pixel,
                "condition_number": geometry.condition_number,
                "warnings": warnings,
            },
            "intermediate": intermediate,
            "error": None,
        }
        if prepared.matching is not None:
            diagnostics = prepared.matching
            result["matching"] = {
                "input_schema_version": diagnostics.input_schema_version,
                "calibration_detection_count": diagnostics.calibration_detection_count,
                "measurement_detection_count": diagnostics.measurement_detection_count,
                "matched_spot_count": diagnostics.matched_spot_count,
                "unmatched_calibration_count": diagnostics.unmatched_calibration_count,
                "unmatched_measurement_count": diagnostics.unmatched_measurement_count,
                "overlap_ratio": diagnostics.overlap_ratio,
                "rmse_pixel": diagnostics.matching_rmse_pixel,
                "max_residual_pixel": diagnostics.matching_max_residual_pixel,
                "hypothesis_margin": diagnostics.hypothesis_margin,
                "min_pair_confidence_product": diagnostics.min_pair_confidence_product,
                "identity_source": "m3_conservative_cross_image_matching",
            }
        return result
    except CoordinateSystemError as error:
        return _error(task_id, "COORDINATE_SYSTEM_INVALID", str(error), True, "GEOMETRY_INVALID")
    except ModelError as error:
        return _error(task_id, "CONFIG_INVALID", str(error), False, "MODEL_INVALID")
    except CalculationError as error:
        return _error(task_id, "CALCULATION_FAILED", str(error), True, str(error))
    except Exception as error:
        return _error(task_id, "UNKNOWN_ERROR", str(error), False, "UNEXPECTED_EXCEPTION")
