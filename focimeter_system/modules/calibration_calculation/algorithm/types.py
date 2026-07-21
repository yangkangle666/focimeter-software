"""Immutable internal types for the M3 calculation pipeline."""

from __future__ import annotations

from dataclasses import dataclass
import math
import re
from typing import Mapping

import numpy as np


class CoordinateSystemError(ValueError):
    """The spot geometry cannot define a usable coordinate system."""


class CalibrationDataError(ValueError):
    """A standard-lens dataset cannot identify a calibration model."""


class ModelError(ValueError):
    """A calibration model is malformed or incompatible."""


class CalculationError(ValueError):
    """A validated input cannot produce a trustworthy result."""


@dataclass(frozen=True)
class PowerVector:
    M: float
    J0: float
    J45: float

    def as_array(self) -> np.ndarray:
        return np.asarray([self.M, self.J0, self.J45], dtype=float)


@dataclass(frozen=True)
class Prescription:
    S: float
    C: float
    A: float | None


@dataclass(frozen=True)
class GeometryFit:
    transform: np.ndarray
    rank: int
    condition_number: float
    rmse_pixel: float
    max_residual_pixel: float
    min_spot_confidence: float
    shifts: Mapping[str, tuple[float, float]]


@dataclass(frozen=True)
class QualityLimits:
    validated_sphere_range_D: tuple[float, float]
    validated_abs_cylinder_max_D: float
    cylinder_threshold_D: float
    max_fit_rmse_pixel: float
    max_condition_number: float
    max_skew_power_D: float
    min_confidence: float
    validation_confidence: float


@dataclass(frozen=True)
class CalibrationModel:
    schema_version: str
    model_type: str
    model_id: str
    validation_status: str
    source_dataset_sha256: str
    distance_m: float
    expected_spot_count: int
    correction_matrix: np.ndarray
    correction_bias: np.ndarray
    quality_limits: QualityLimits
    fit_metrics: Mapping[str, object]
    standard_lenses: tuple[Mapping[str, object], ...]

    @classmethod
    def from_dict(cls, data: Mapping[str, object]) -> "CalibrationModel":
        try:
            hardware = data["hardware"]
            correction = data["correction"]
            limits = data["quality_limits"]
            matrix = np.asarray(correction["matrix"], dtype=float)
            bias = np.asarray(correction["bias"], dtype=float)
            quality = QualityLimits(
                validated_sphere_range_D=tuple(float(x) for x in limits["validated_sphere_range_D"]),
                validated_abs_cylinder_max_D=float(limits["validated_abs_cylinder_max_D"]),
                cylinder_threshold_D=float(limits["cylinder_threshold_D"]),
                max_fit_rmse_pixel=float(limits["max_fit_rmse_pixel"]),
                max_condition_number=float(limits["max_condition_number"]),
                max_skew_power_D=float(limits["max_skew_power_D"]),
                min_confidence=float(limits["min_confidence"]),
                validation_confidence=float(limits["validation_confidence"]),
            )
            model = cls(
                schema_version=str(data["schema_version"]),
                model_type=str(data["model_type"]),
                model_id=str(data["model_id"]),
                validation_status=str(data["validation_status"]),
                source_dataset_sha256=str(data["source_dataset_sha256"]),
                distance_m=float(hardware["distance_m"]),
                expected_spot_count=int(hardware["expected_spot_count"]),
                correction_matrix=matrix,
                correction_bias=bias,
                quality_limits=quality,
                fit_metrics=dict(data["fit_metrics"]),
                standard_lenses=tuple(dict(item) for item in data["standard_lenses"]),
            )
        except (KeyError, TypeError, ValueError) as error:
            raise ModelError(f"Invalid calibration model: {error}") from error
        model._validate()
        return model

    def _validate(self) -> None:
        if self.schema_version != "1.0" or self.model_type != "hybrid_power_matrix_v1":
            raise ModelError("Unsupported calibration model version or type.")
        if self.validation_status not in {"simulation_only", "metrology_validated"}:
            raise ModelError("Unknown calibration model validation_status.")
        if not self.model_id or re.fullmatch(r"[0-9a-f]{64}", self.source_dataset_sha256) is None:
            raise ModelError("Model ID and source dataset SHA-256 are required.")
        if self.correction_matrix.shape != (3, 3) or self.correction_bias.shape != (3,):
            raise ModelError("Correction matrix must be 3x3 and bias must contain three values.")
        numeric = np.concatenate([self.correction_matrix.ravel(), self.correction_bias])
        if not np.all(np.isfinite(numeric)):
            raise ModelError("Correction values must be finite.")
        low, high = self.quality_limits.validated_sphere_range_D
        if (
            not math.isfinite(low)
            or not math.isfinite(high)
            or low >= high
            or not math.isfinite(self.distance_m)
            or self.distance_m <= 0
            or self.expected_spot_count < 4
        ):
            raise ModelError("Hardware fingerprint or validated sphere range is invalid.")
        positive = (
            self.quality_limits.validated_abs_cylinder_max_D,
            self.quality_limits.max_fit_rmse_pixel,
            self.quality_limits.max_condition_number,
            self.quality_limits.max_skew_power_D,
        )
        if any(value <= 0 or not np.isfinite(value) for value in positive):
            raise ModelError("Model quality limits must be positive finite values.")
        if (
            not np.isfinite(self.quality_limits.cylinder_threshold_D)
            or self.quality_limits.cylinder_threshold_D < 0
            or self.quality_limits.max_condition_number < 1
        ):
            raise ModelError("cylinder_threshold_D cannot be negative.")
        for value in (self.quality_limits.min_confidence, self.quality_limits.validation_confidence):
            if not 0 <= value <= 1:
                raise ModelError("Confidence limits must be between zero and one.")

    def to_dict(self) -> dict[str, object]:
        limits = self.quality_limits
        return {
            "schema_version": self.schema_version,
            "model_type": self.model_type,
            "model_id": self.model_id,
            "validation_status": self.validation_status,
            "source_dataset_sha256": self.source_dataset_sha256,
            "hardware": {"distance_m": self.distance_m, "expected_spot_count": self.expected_spot_count},
            "correction": {
                "matrix": self.correction_matrix.tolist(),
                "bias": self.correction_bias.tolist(),
            },
            "quality_limits": {
                "validated_sphere_range_D": list(limits.validated_sphere_range_D),
                "validated_abs_cylinder_max_D": limits.validated_abs_cylinder_max_D,
                "cylinder_threshold_D": limits.cylinder_threshold_D,
                "max_fit_rmse_pixel": limits.max_fit_rmse_pixel,
                "max_condition_number": limits.max_condition_number,
                "max_skew_power_D": limits.max_skew_power_D,
                "min_confidence": limits.min_confidence,
                "validation_confidence": limits.validation_confidence,
            },
            "fit_metrics": dict(self.fit_metrics),
            "standard_lenses": [dict(item) for item in self.standard_lenses],
        }
