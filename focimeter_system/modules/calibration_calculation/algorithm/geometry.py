"""Spot correspondence and weighted two-dimensional geometry fitting."""

from __future__ import annotations

from typing import Mapping

import numpy as np

from .types import CoordinateSystemError, GeometryFit


def _spots_by_id(document: Mapping[str, object]) -> dict[int, Mapping[str, object]]:
    try:
        spots = document["spots"]
        mapped = {int(spot["spot_id"]): spot for spot in spots}
    except (KeyError, TypeError, ValueError) as error:
        raise CoordinateSystemError(f"Invalid spot collection: {error}") from error
    if len(mapped) != len(spots):
        raise CoordinateSystemError("spot_id values must be unique.")
    return mapped


def _spot_by_role(document: Mapping[str, object], role: str) -> Mapping[str, object]:
    matches = [spot for spot in document["spots"] if spot["role"] == role]
    if len(matches) != 1:
        raise CoordinateSystemError(f"Role '{role}' must appear exactly once.")
    return matches[0]


def _point(spot: Mapping[str, object]) -> np.ndarray:
    value = np.asarray([spot["x"], spot["y"]], dtype=float)
    if value.shape != (2,) or not np.all(np.isfinite(value)):
        raise CoordinateSystemError("Spot coordinates must be finite two-dimensional values.")
    return value


def _orthonormal_basis(calibration: Mapping[str, object]) -> tuple[np.ndarray, np.ndarray]:
    center = _point(_spot_by_role(calibration, "center"))
    x_hint = _point(_spot_by_role(calibration, "x_positive")) - center
    y_hint = _point(_spot_by_role(calibration, "y_positive")) - center
    scale = max(float(np.linalg.norm(x_hint)), float(np.linalg.norm(y_hint)), 1.0)
    if np.linalg.norm(x_hint) <= np.finfo(float).eps * scale:
        raise CoordinateSystemError("x_positive cannot define the calibration X axis.")
    ex = x_hint / np.linalg.norm(x_hint)
    y_orthogonal = y_hint - np.dot(y_hint, ex) * ex
    if np.linalg.norm(y_orthogonal) <= np.finfo(float).eps * scale * 100:
        raise CoordinateSystemError("x_positive and y_positive are collinear.")
    ey = y_orthogonal / np.linalg.norm(y_orthogonal)
    if np.dot(ey, y_hint) < 0:
        ey = -ey
    return center, np.column_stack([ex, ey])


def fit_spot_transform(
    calibration: Mapping[str, object],
    measurement: Mapping[str, object],
) -> GeometryFit:
    """Fit `measurement ~= transform @ calibration` after removing each center."""

    calibration_spots = _spots_by_id(calibration)
    measurement_spots = _spots_by_id(measurement)
    if set(calibration_spots) != set(measurement_spots):
        raise CoordinateSystemError("Calibration and measurement spot_id sets must match.")
    for spot_id in calibration_spots:
        if calibration_spots[spot_id]["role"] != measurement_spots[spot_id]["role"]:
            raise CoordinateSystemError(f"Role changed for spot_id {spot_id}.")

    calibration_center, basis = _orthonormal_basis(calibration)
    measurement_center = _point(_spot_by_role(measurement, "center"))
    center_id = int(_spot_by_role(calibration, "center")["spot_id"])
    outer_ids = sorted(spot_id for spot_id in calibration_spots if spot_id != center_id)
    if len(outer_ids) < 3:
        raise CoordinateSystemError("At least three paired non-center spots are required.")

    x_rows: list[np.ndarray] = []
    y_rows: list[np.ndarray] = []
    weights: list[float] = []
    shifts: dict[str, tuple[float, float]] = {}
    for spot_id in outer_ids:
        calib_spot = calibration_spots[spot_id]
        meas_spot = measurement_spots[spot_id]
        calib_vector = _point(calib_spot) - calibration_center
        meas_vector = _point(meas_spot) - measurement_center
        x_rows.append(basis.T @ calib_vector)
        y_rows.append(basis.T @ meas_vector)
        confidence = float(calib_spot["confidence"]) * float(meas_spot["confidence"])
        if not np.isfinite(confidence) or confidence <= 0:
            raise CoordinateSystemError("Paired spot confidence must be positive and finite.")
        weights.append(confidence)
        role = str(calib_spot["role"])
        if role in {"x_positive", "y_positive"}:
            shift = meas_vector - calib_vector
            shifts[role] = (float(shift[0]), float(shift[1]))

    x = np.asarray(x_rows, dtype=float)
    y = np.asarray(y_rows, dtype=float)
    root_weights = np.sqrt(np.asarray(weights, dtype=float))[:, None]
    weighted_x = x * root_weights
    weighted_y = y * root_weights
    transform_t, _, rank, _ = np.linalg.lstsq(weighted_x, weighted_y, rcond=None)
    if rank != 2:
        raise CoordinateSystemError("Non-center calibration spots are collinear.")
    transform = transform_t.T
    predicted = x @ transform.T
    residual = y - predicted
    residual_norms = np.linalg.norm(residual, axis=1)
    weighted_square = np.asarray(weights) * residual_norms**2
    rmse = float(np.sqrt(weighted_square.sum() / np.asarray(weights).sum()))
    condition_number = float(np.linalg.cond(weighted_x))
    if not np.all(np.isfinite(transform)) or not np.isfinite(condition_number):
        raise CoordinateSystemError("Spot transform is numerically invalid.")
    minimum_confidence = min(
        float(calibration_spots[spot_id]["confidence"])
        for spot_id in calibration_spots
    )
    minimum_confidence = min(
        minimum_confidence,
        *(float(measurement_spots[spot_id]["confidence"]) for spot_id in measurement_spots),
    )
    return GeometryFit(
        transform=transform,
        rank=int(rank),
        condition_number=condition_number,
        rmse_pixel=rmse,
        max_residual_pixel=float(residual_norms.max(initial=0.0)),
        min_spot_confidence=minimum_confidence,
        shifts=shifts,
    )
