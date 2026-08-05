"""Conservative cross-image matching for experimental Hartmann spot lattices."""

from __future__ import annotations

from dataclasses import dataclass
import itertools
import math
from typing import Iterable

import numpy as np

from .experimental_input import ExperimentalObservation, ExperimentalPair
from .types import CoordinateSystemError, MatchingLimits


@dataclass(frozen=True)
class LatticeAssignment:
    coordinates: tuple[tuple[int, int], ...]
    pitch: float
    basis: np.ndarray
    rmse_pitch_ratio: float


@dataclass(frozen=True)
class MatchDiagnostics:
    input_schema_version: str
    calibration_detection_count: int
    measurement_detection_count: int
    matched_spot_count: int
    unmatched_calibration_count: int
    unmatched_measurement_count: int
    overlap_ratio: float
    matching_rmse_pixel: float
    matching_max_residual_pixel: float
    hypothesis_margin: float


@dataclass(frozen=True)
class MatchedExperimentalPair:
    calibration: dict[str, object]
    measurement: dict[str, object]
    diagnostics: MatchDiagnostics
    detection_pairs: tuple[tuple[int, int, int], ...]


@dataclass(frozen=True)
class _ScoredHypothesis:
    pairs: tuple[tuple[int, int], ...]
    transform: np.ndarray
    translation: np.ndarray
    rmse: float
    maximum_residual: float
    overlap_ratio: float
    score: float


def _points(observations: tuple[ExperimentalObservation, ...]) -> np.ndarray:
    return np.asarray([(item.x, item.y) for item in observations], dtype=float)


def _estimate_pitch(points: np.ndarray) -> float:
    """Estimate one lattice pitch from robust nearest-neighbour distances."""

    if points.ndim != 2 or points.shape[1] != 2 or len(points) < 4:
        raise CoordinateSystemError("At least four two-dimensional spots are required for lattice matching.")
    differences = points[:, None, :] - points[None, :, :]
    distances = np.linalg.norm(differences, axis=2)
    distances[distances <= np.finfo(float).eps] = np.inf
    nearest = np.min(distances, axis=1)
    pitch = float(np.median(nearest))
    if not math.isfinite(pitch) or pitch <= np.finfo(float).eps:
        raise CoordinateSystemError("Experimental spots do not define a finite lattice pitch.")
    # A fake isolated point can have a large nearest distance; a merged/duplicate point
    # can have a very small one. Both make the topology unsafe.
    central = nearest[(nearest >= 0.55 * pitch) & (nearest <= 1.45 * pitch)]
    if len(central) < max(4, math.ceil(0.75 * len(points))):
        raise CoordinateSystemError("Nearest-neighbour distances are inconsistent with one spot lattice.")
    return float(np.median(central))


def _candidate_bases(
    points: np.ndarray,
    pitch: float,
) -> tuple[np.ndarray, ...]:
    """Recover image-oriented horizontal/vertical lattice vectors."""

    vectors = points[:, None, :] - points[None, :, :]
    flat = vectors.reshape(-1, 2)
    lengths = np.linalg.norm(flat, axis=1)
    neighbours = flat[(lengths >= 0.72 * pitch) & (lengths <= 1.28 * pitch)]
    angles = np.arctan2(neighbours[:, 1], neighbours[:, 0])
    fourfold_direction = np.asarray([
        float(np.cos(4.0 * angles).sum()),
        float(np.sin(4.0 * angles).sum()),
    ])
    if float(np.linalg.norm(fourfold_direction)) <= np.finfo(float).eps * len(neighbours):
        raise CoordinateSystemError("Spot topology cannot recover a stable lattice orientation.")
    axis_angle = 0.25 * math.atan2(fourfold_direction[1], fourfold_direction[0])
    first_direction = np.asarray([math.cos(axis_angle), math.sin(axis_angle)])
    second_direction = np.asarray([-math.sin(axis_angle), math.cos(axis_angle)])
    first_mask = np.abs(neighbours @ first_direction) >= np.abs(neighbours @ second_direction)
    first_axis = neighbours[first_mask]
    second_axis = neighbours[~first_mask]
    if len(first_axis) < 2 or len(second_axis) < 2:
        raise CoordinateSystemError("Spot topology cannot recover both lattice axes.")
    first_axis = np.where((first_axis @ first_direction)[:, None] < 0, -first_axis, first_axis)
    second_axis = np.where((second_axis @ second_direction)[:, None] < 0, -second_axis, second_axis)
    basis = np.column_stack([
        np.median(first_axis, axis=0),
        np.median(second_axis, axis=0),
    ])
    if abs(float(np.linalg.det(basis))) <= np.finfo(float).eps * pitch * pitch:
        raise CoordinateSystemError("Recovered lattice axes are degenerate.")
    # Absolute installation angle is not a cross-image rotation. The relative
    # calibration-to-measurement rotation is constrained in _fit_candidate.
    return (basis,)


def _align_measurement_basis(
    calibration_basis: np.ndarray,
    measurement_basis: np.ndarray,
    max_relative_rotation_degree: float,
) -> np.ndarray:
    """Choose the measurement quarter-turn representation closest to the reference axes."""

    first = measurement_basis[:, 0]
    second = measurement_basis[:, 1]
    equivalents = (
        measurement_basis,
        np.column_stack([second, -first]),
        -measurement_basis,
        np.column_stack([-second, first]),
    )
    candidates: list[tuple[float, np.ndarray]] = []
    inverse_calibration = np.linalg.inv(calibration_basis)
    for basis in equivalents:
        relative = basis @ inverse_calibration
        if float(np.linalg.det(relative)) <= 0:
            continue
        u, _, vt = np.linalg.svd(relative)
        rotation_matrix = u @ vt
        angle = abs(math.degrees(math.atan2(
            float(rotation_matrix[1, 0]),
            float(rotation_matrix[0, 0]),
        )))
        candidates.append((angle, basis))
    if not candidates:
        raise CoordinateSystemError("Lattice axes cannot be aligned without reflection.")
    candidates.sort(key=lambda item: item[0])
    if candidates[0][0] > max_relative_rotation_degree:
        raise CoordinateSystemError("Relative lattice rotation exceeds the conservative matching range.")
    return candidates[0][1]


def _assign_lattice(
    points: np.ndarray,
    basis: np.ndarray,
    limits: MatchingLimits,
) -> LatticeAssignment:
    """Assign integer topology and refine its affine embedding."""

    anchor = points[0]
    projected = (np.linalg.inv(basis) @ (points - anchor).T).T
    coordinates = np.rint(projected).astype(int)
    design = np.column_stack([coordinates, np.ones(len(coordinates))])
    coefficients, _, rank, _ = np.linalg.lstsq(design, points, rcond=None)
    if rank != 3:
        raise CoordinateSystemError("Spot lattice is rank deficient.")
    predicted = design @ coefficients
    residuals = np.linalg.norm(points - predicted, axis=1)
    refined_basis = coefficients[:2, :].T
    pitch = float(np.mean(np.linalg.norm(refined_basis, axis=0)))
    if len({tuple(value) for value in coordinates}) != len(coordinates):
        raise CoordinateSystemError("Multiple detections collide at one lattice coordinate.")
    ratio = float(np.sqrt(np.mean(residuals**2)) / pitch)
    if ratio > limits.max_lattice_residual_pitch_ratio or float(residuals.max()) / pitch > 2.0 * limits.max_lattice_residual_pitch_ratio:
        raise CoordinateSystemError("Spot positions do not fit one lattice within the configured residual limit.")
    return LatticeAssignment(
        coordinates=tuple((int(value[0]), int(value[1])) for value in coordinates),
        pitch=pitch,
        basis=refined_basis,
        rmse_pitch_ratio=ratio,
    )


def _fit_candidate(
    calibration_points: np.ndarray,
    measurement_points: np.ndarray,
    calibration_observations: tuple[ExperimentalObservation, ...],
    measurement_observations: tuple[ExperimentalObservation, ...],
    pairs: tuple[tuple[int, int], ...],
    pitch: float,
    limits: MatchingLimits,
) -> _ScoredHypothesis | None:
    if len(pairs) < limits.min_matched_spots:
        return None
    # Every detected point must receive one unique cross-image identity. Using
    # only a matchable subset could silently discard a false or misidentified ray.
    if len(pairs) != len(calibration_points) or len(pairs) != len(measurement_points):
        return None
    overlap = 1.0
    if overlap < limits.min_overlap_ratio:
        return None
    c = calibration_points[[item[0] for item in pairs]]
    m = measurement_points[[item[1] for item in pairs]]
    confidence = np.asarray([
        calibration_observations[ci].confidence * measurement_observations[mi].confidence
        for ci, mi in pairs
    ])
    if float(confidence.min()) < limits.min_confidence:
        return None
    design = np.column_stack([c, np.ones(len(c))])
    weighted_design = design * np.sqrt(confidence)[:, None]
    weighted_target = m * np.sqrt(confidence)[:, None]
    coefficients, _, rank, _ = np.linalg.lstsq(weighted_design, weighted_target, rcond=None)
    if rank != 3:
        return None
    transform = coefficients[:2, :].T
    translation = coefficients[2, :]
    if float(np.linalg.det(transform)) <= 0:
        return None
    condition = float(np.linalg.cond(transform))
    if not math.isfinite(condition) or condition > limits.max_condition_number:
        return None
    u, singular, vt = np.linalg.svd(transform)
    rotation_matrix = u @ vt
    rotation = abs(math.degrees(math.atan2(float(rotation_matrix[1, 0]), float(rotation_matrix[0, 0]))))
    if rotation > limits.max_rotation_degree:
        return None
    if float(singular.min()) < limits.min_scale_ratio or float(singular.max()) > limits.max_scale_ratio:
        return None
    symmetric = vt.T @ np.diag(singular) @ vt
    shear = abs(float(symmetric[0, 1])) / max(float(np.mean(np.diag(symmetric))), np.finfo(float).eps)
    if shear > limits.max_shear_ratio:
        return None
    calibration_center = np.average(c, axis=0, weights=confidence)
    center_displacement = transform @ calibration_center + translation - calibration_center
    if float(np.linalg.norm(center_displacement)) / pitch > limits.max_translation_pitch_ratio:
        return None
    predicted = c @ transform.T + translation
    residuals = np.linalg.norm(m - predicted, axis=1)
    rmse = float(np.sqrt(np.sum(confidence * residuals**2) / np.sum(confidence)))
    maximum = float(residuals.max(initial=0.0))
    if rmse / pitch > limits.max_matching_rmse_pitch_ratio:
        return None
    if maximum / pitch > limits.max_matching_residual_pitch_ratio:
        return None
    score = rmse / pitch + maximum / pitch
    return _ScoredHypothesis(pairs, transform, translation, rmse, maximum, overlap, score)


def _candidate_matches(
    calibration: LatticeAssignment,
    measurement: LatticeAssignment,
) -> Iterable[tuple[tuple[int, int], ...]]:
    """Enumerate topology-preserving integer offsets, independent of array order."""

    calibration_by_coordinate = {coordinate: index for index, coordinate in enumerate(calibration.coordinates)}
    measurement_coordinates = measurement.coordinates
    offsets = {
        (calibration_coordinate[0] - measurement_coordinate[0], calibration_coordinate[1] - measurement_coordinate[1])
        for calibration_coordinate, measurement_coordinate in itertools.product(
            calibration.coordinates, measurement_coordinates
        )
    }
    seen: set[tuple[tuple[int, int], ...]] = set()
    for dx, dy in offsets:
        pairs = tuple(sorted(
            (calibration_by_coordinate[(coordinate[0] + dx, coordinate[1] + dy)], measurement_index)
            for measurement_index, coordinate in enumerate(measurement_coordinates)
            if (coordinate[0] + dx, coordinate[1] + dy) in calibration_by_coordinate
        ))
        if pairs and pairs not in seen:
            seen.add(pairs)
            yield pairs


def _lattice_identity_aliases(coordinates: tuple[tuple[int, int], ...]) -> frozenset[str]:
    """Return non-identity square-lattice symmetries that preserve the point set."""

    values = np.asarray(coordinates, dtype=int)

    def normalized(points: np.ndarray) -> tuple[tuple[int, int], ...]:
        shifted = points - np.min(points, axis=0)
        return tuple(sorted((int(point[0]), int(point[1])) for point in shifted))

    reference = normalized(values)
    transforms = {
        "rotation_90": np.asarray([[0, -1], [1, 0]]),
        "rotation_180": np.asarray([[-1, 0], [0, -1]]),
        "rotation_270": np.asarray([[0, 1], [-1, 0]]),
        "reflection_x": np.asarray([[-1, 0], [0, 1]]),
        "reflection_y": np.asarray([[1, 0], [0, -1]]),
        "reflection_diagonal": np.asarray([[0, 1], [1, 0]]),
        "reflection_antidiagonal": np.asarray([[0, -1], [-1, 0]]),
    }
    return frozenset(
        name
        for name, transform in transforms.items()
        if normalized(values @ transform.T) == reference
    )


def _document(task_id: str, image_type: str, spots: list[dict[str, object]]) -> dict[str, object]:
    minimum_confidence = min(float(spot["confidence"]) for spot in spots)
    return {
        "schema_version": "1.0",
        "task_id": task_id,
        "module": "m2_image_recognition",
        "status": "ok",
        "image_type": image_type,
        "coordinate_type": "image_pixel",
        "spots": spots,
        "quality": {
            "expected_count": len(spots),
            "detected_count": len(spots),
            "min_confidence": minimum_confidence,
            "is_usable": True,
            "warnings": ["M2_EXPERIMENTAL_MULTISPOT", "software_verified"],
        },
        "error": None,
    }


def match_experimental_multispot(
    pair: ExperimentalPair,
    limits: MatchingLimits,
) -> MatchedExperimentalPair:
    """Require one unique cross-image identity for every detected spot."""

    if len(pair.calibration) != len(pair.measurement):
        raise CoordinateSystemError(
            "Every detected spot requires a unique cross-image identity; "
            f"calibration has {len(pair.calibration)} detections and measurement has "
            f"{len(pair.measurement)}."
        )

    calibration_points = _points(pair.calibration)
    measurement_points = _points(pair.measurement)
    calibration_pitch = _estimate_pitch(calibration_points)
    measurement_pitch = _estimate_pitch(measurement_points)
    calibration_basis = _candidate_bases(calibration_points, calibration_pitch)[0]
    measurement_basis = _align_measurement_basis(
        calibration_basis,
        _candidate_bases(measurement_points, measurement_pitch)[0],
        limits.max_rotation_degree,
    )
    calibration_lattice = _assign_lattice(
        calibration_points,
        calibration_basis,
        limits,
    )
    measurement_lattice = _assign_lattice(
        measurement_points,
        measurement_basis,
        limits,
    )
    pitch = 0.5 * (calibration_lattice.pitch + measurement_lattice.pitch)
    shared_aliases = sorted(
        _lattice_identity_aliases(calibration_lattice.coordinates)
        & _lattice_identity_aliases(measurement_lattice.coordinates)
    )
    if shared_aliases:
        raise CoordinateSystemError(
            "Unmarked symmetric lattices have unresolved physical-ray identity aliases: "
            f"{shared_aliases}."
        )
    hypotheses = tuple(_candidate_matches(calibration_lattice, measurement_lattice))
    maximum_topological_overlap = max((len(item) for item in hypotheses), default=0)
    if maximum_topological_overlap < len(pair.calibration):
        raise CoordinateSystemError(
            f"Cross-image matching assigned {maximum_topological_overlap} of "
            f"{len(pair.calibration)} detections; every calibration and measurement "
            "detection requires one unique physical identity."
        )
    candidates = [
        scored
        for hypothesis in hypotheses
        if len(hypothesis) == maximum_topological_overlap
        if (scored := _fit_candidate(
            calibration_points,
            measurement_points,
            pair.calibration,
            pair.measurement,
            hypothesis,
            pitch,
            limits,
        )) is not None
    ]
    if not candidates:
        raise CoordinateSystemError("No cross-image lattice hypothesis satisfies the conservative matching limits.")
    candidates.sort(key=lambda item: (-len(item.pairs), item.score))
    best = candidates[0]
    alternatives = [item for item in candidates[1:] if item.pairs != best.pairs and len(item.pairs) == len(best.pairs)]
    if alternatives:
        second = alternatives[0]
        margin = (second.score - best.score) / max(second.score, np.finfo(float).eps)
        if margin < limits.minimum_hypothesis_margin:
            raise CoordinateSystemError("Cross-image lattice identity is ambiguous between equivalent hypotheses.")
    else:
        margin = 1.0

    ordered_pairs = sorted(
        best.pairs,
        key=lambda item: (calibration_lattice.coordinates[item[0]][1], calibration_lattice.coordinates[item[0]][0]),
    )
    calibration_spots: list[dict[str, object]] = []
    measurement_spots: list[dict[str, object]] = []
    detection_pairs: list[tuple[int, int, int]] = []
    for spot_id, (calibration_index, measurement_index) in enumerate(ordered_pairs):
        calibration_observation = pair.calibration[calibration_index]
        measurement_observation = pair.measurement[measurement_index]
        calibration_spots.append({
            "spot_id": spot_id,
            "role": "unknown",
            "x": calibration_observation.x,
            "y": calibration_observation.y,
            "confidence": calibration_observation.confidence,
        })
        measurement_spots.append({
            "spot_id": spot_id,
            "role": "unknown",
            "x": measurement_observation.x,
            "y": measurement_observation.y,
            "confidence": measurement_observation.confidence,
        })
        detection_pairs.append((spot_id, calibration_observation.detection_id, measurement_observation.detection_id))

    diagnostics = MatchDiagnostics(
        input_schema_version="m2.multispot.experimental.1",
        calibration_detection_count=len(pair.calibration),
        measurement_detection_count=len(pair.measurement),
        matched_spot_count=len(ordered_pairs),
        unmatched_calibration_count=len(pair.calibration) - len(ordered_pairs),
        unmatched_measurement_count=len(pair.measurement) - len(ordered_pairs),
        overlap_ratio=best.overlap_ratio,
        matching_rmse_pixel=best.rmse,
        matching_max_residual_pixel=best.maximum_residual,
        hypothesis_margin=margin,
    )
    return MatchedExperimentalPair(
        calibration=_document(pair.task_id, "calibration", calibration_spots),
        measurement=_document(pair.task_id, "measurement", measurement_spots),
        diagnostics=diagnostics,
        detection_pairs=tuple(detection_pairs),
    )
