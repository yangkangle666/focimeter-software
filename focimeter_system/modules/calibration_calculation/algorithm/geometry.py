"""Spot correspondence and weighted two-dimensional geometry fitting."""

from __future__ import annotations

from typing import Mapping

import numpy as np

from .types import CoordinateSystemError, GeometryFit


def _spots_by_id(document: Mapping[str, object]) -> dict[int, Mapping[str, object]]:
    try:
        spots = document["spots"]
        mapped = {int(spot["spot_id"]): spot for spot in spots}
    except (KeyError, TypeError) as error:
        raise CoordinateSystemError(f"Invalid spot collection: {error}") from error
    if len(mapped) != len(spots):
        raise CoordinateSystemError("Spot IDs must be unique for pairing.")
    return mapped


def _spot_by_role(document: Mapping[str, object], role: str) -> Mapping[str, object]:
    matches = [spot for spot in document["spots"] if spot.get("role") == role]
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
    if np.linalg.norm(y_hint) <= np.finfo(float).eps * scale:
        raise CoordinateSystemError("y_positive cannot define the calibration Y axis.")
    ey = y_hint / np.linalg.norm(y_hint)
    ex = np.asarray([-ey[1], ey[0]], dtype=float)
    x_projection = float(np.dot(x_hint, ex))
    if abs(x_projection) <= np.finfo(float).eps * scale * 100:
        raise CoordinateSystemError("x_positive cannot validate the calibration X axis.")
    if x_projection < 0:
        ex = -ex
    return center, np.column_stack([ex, ey])


def _weighted_center(
    spots: Mapping[int, Mapping[str, object]],
    weights: Mapping[int, float],
) -> np.ndarray:
    total_weight = float(sum(weights.values()))
    if total_weight <= 0 or not np.isfinite(total_weight):
        raise CoordinateSystemError("Paired spot weights must be positive and finite.")
    return sum(_point(spot) * weights[spot_id] for spot_id, spot in spots.items()) / total_weight


def _basis_and_centers(
    calibration_spots: Mapping[int, Mapping[str, object]],
    measurement_spots: Mapping[int, Mapping[str, object]],
    weights: Mapping[int, float],
) -> tuple[np.ndarray, np.ndarray, np.ndarray, int | None]:
    center_ids = [
        spot_id for spot_id, spot in calibration_spots.items() if spot.get("role") == "center"
    ]
    if len(center_ids) == 1:
        center_id = center_ids[0]
        try:
            calibration_center, basis = _orthonormal_basis({"spots": list(calibration_spots.values())})
        except CoordinateSystemError:
            calibration_center = _point(calibration_spots[center_id])
            basis = np.eye(2, dtype=float)
        return calibration_center, _point(measurement_spots[center_id]), basis, center_id

    calibration_center = _weighted_center(calibration_spots, weights)
    measurement_center = _weighted_center(measurement_spots, weights)
    return calibration_center, measurement_center, np.eye(2, dtype=float), None


def fit_spot_transform(
    calibration: Mapping[str, object],
    measurement: Mapping[str, object],
) -> GeometryFit:
    """Fit `measurement ~= transform @ calibration` after removing each center."""

    calibration_spots = _spots_by_id(calibration)
    measurement_spots = _spots_by_id(measurement)
    if set(calibration_spots) != set(measurement_spots):
        raise CoordinateSystemError("Calibration and measurement spot ID sets must match.")
    pair_weights: dict[int, float] = {}
    for spot_id in calibration_spots:
        calibration_role = calibration_spots[spot_id].get("role")
        measurement_role = measurement_spots[spot_id].get("role")
        if (
            calibration_role is not None
            and measurement_role is not None
            and calibration_role != "unknown"
            and measurement_role != "unknown"
            and calibration_role != measurement_role
        ):
            raise CoordinateSystemError(
                f"Spot ID {spot_id} must preserve a known role across calibration and measurement."
            )
        confidence = float(calibration_spots[spot_id]["confidence"]) * float(
            measurement_spots[spot_id]["confidence"]
        )
        if not np.isfinite(confidence) or confidence <= 0:
            raise CoordinateSystemError("Paired spot confidence must be positive and finite.")
        pair_weights[spot_id] = confidence

    calibration_center, measurement_center, basis, center_id = _basis_and_centers(
        calibration_spots, measurement_spots, pair_weights
    )
    paired_ids = sorted(spot_id for spot_id in calibration_spots if spot_id != center_id)
    if len(paired_ids) < 3:
        raise CoordinateSystemError("At least three paired non-center or four paired multi-spot points are required.")

    x_rows: list[np.ndarray] = []
    y_rows: list[np.ndarray] = []
    weights: list[float] = []
    shifts: dict[str, tuple[float, float]] = {}
    for spot_id in paired_ids:
        calib_spot = calibration_spots[spot_id]
        meas_spot = measurement_spots[spot_id]
        role = str(calib_spot.get("role", ""))
        calib_vector = _point(calib_spot) - calibration_center
        meas_vector = _point(meas_spot) - measurement_center
        x_rows.append(basis.T @ calib_vector)
        y_rows.append(basis.T @ meas_vector)
        confidence = pair_weights[spot_id]
        weights.append(confidence)
        if role in {"x_positive", "y_positive"}:
            shift = basis.T @ (meas_vector - calib_vector)
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
    if np.linalg.det(transform) <= 0:
        raise CoordinateSystemError("Spot transform reverses orientation.")
    if any(float(np.dot(source, target)) <= 0 for source, target in zip(x, y, strict=True)):
        raise CoordinateSystemError("A paired outer spot reverses direction.")
    minimum_confidence = min(
        float(spot["confidence"])
        for spot in calibration_spots.values()
    )
    minimum_confidence = min(
        minimum_confidence,
        *(float(spot["confidence"]) for spot in measurement_spots.values()),
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
