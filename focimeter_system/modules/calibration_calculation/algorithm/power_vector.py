"""Conversions between spot transforms, power matrices, and prescriptions."""

from __future__ import annotations

import math

import numpy as np

from .types import CalculationError, PowerVector, Prescription


def power_matrix_from_transform(transform: np.ndarray, distance_m: float) -> tuple[np.ndarray, float]:
    transform = np.asarray(transform, dtype=float)
    if transform.shape != (2, 2) or not np.all(np.isfinite(transform)):
        raise CalculationError("Spot transform must be a finite 2x2 matrix.")
    if not math.isfinite(distance_m) or distance_m <= 0:
        raise CalculationError("distance_m must be a positive finite value.")
    raw = (np.eye(2) - transform) / distance_m
    symmetric = (raw + raw.T) / 2.0
    skew = (raw - raw.T) / 2.0
    return symmetric, float(np.linalg.norm(skew, ord="fro"))


def matrix_to_power_vector(matrix: np.ndarray) -> PowerVector:
    matrix = np.asarray(matrix, dtype=float)
    if matrix.shape != (2, 2) or not np.all(np.isfinite(matrix)):
        raise CalculationError("Power matrix must be finite and 2x2.")
    symmetric = (matrix + matrix.T) / 2.0
    return PowerVector(
        M=float((symmetric[0, 0] + symmetric[1, 1]) / 2.0),
        J0=float((symmetric[0, 0] - symmetric[1, 1]) / 2.0),
        J45=float(symmetric[0, 1]),
    )


def power_vector_to_matrix(vector: PowerVector) -> np.ndarray:
    values = vector.as_array()
    if not np.all(np.isfinite(values)):
        raise CalculationError("Power vector must contain finite values.")
    return np.asarray(
        [[vector.M + vector.J0, vector.J45], [vector.J45, vector.M - vector.J0]],
        dtype=float,
    )


def prescription_to_power_vector(prescription: Prescription) -> PowerVector:
    if prescription.C > 0:
        raise CalculationError("Convert positive-cylinder prescriptions to minus-cylinder notation first.")
    if prescription.C == 0:
        return PowerVector(float(prescription.S), 0.0, 0.0)
    if prescription.A is None:
        raise CalculationError("A non-spherical prescription requires an axis.")
    angle = math.radians(float(prescription.A) % 180.0)
    magnitude = -float(prescription.C) / 2.0
    return PowerVector(
        M=float(prescription.S + prescription.C / 2.0),
        J0=magnitude * math.cos(2.0 * angle),
        J45=magnitude * math.sin(2.0 * angle),
    )


def power_vector_to_prescription(vector: PowerVector, cylinder_threshold_D: float) -> Prescription:
    if cylinder_threshold_D < 0 or not math.isfinite(cylinder_threshold_D):
        raise CalculationError("cylinder_threshold_D must be finite and non-negative.")
    if not np.all(np.isfinite(vector.as_array())):
        raise CalculationError("Power vector must contain finite values.")
    magnitude = math.hypot(vector.J0, vector.J45)
    cylinder = -2.0 * magnitude
    if abs(cylinder) <= cylinder_threshold_D:
        return Prescription(S=float(vector.M), C=0.0, A=None)
    sphere = float(vector.M - cylinder / 2.0)
    axis = (math.degrees(0.5 * math.atan2(vector.J45, vector.J0))) % 180.0
    return Prescription(S=sphere, C=cylinder, A=axis)


def transpose_to_minus_cylinder(prescription: Prescription) -> Prescription:
    if prescription.C == 0:
        return Prescription(float(prescription.S), 0.0, None)
    if prescription.C < 0:
        return Prescription(float(prescription.S), float(prescription.C), prescription.A)
    if prescription.A is None:
        raise CalculationError("A positive-cylinder prescription requires an axis.")
    return Prescription(
        S=float(prescription.S + prescription.C),
        C=float(-prescription.C),
        A=float((prescription.A + 90.0) % 180.0),
    )
