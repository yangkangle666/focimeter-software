"""Strict adapter for M2 experimental multispot detection documents."""

from __future__ import annotations

from dataclasses import dataclass
import json
import math
from functools import lru_cache
from pathlib import Path
from typing import Mapping

from jsonschema import Draft202012Validator

from .types import CoordinateSystemError, ModelError


SCHEMA_PATH = Path(__file__).resolve().parents[1] / "schemas" / "m2_multispot_experimental.schema.json"
EXPERIMENTAL_SCHEMA_VERSION = "m2.multispot.experimental.1"
IDENTITY_SAFE_QUALITY_FLAGS = frozenset({"SATURATED_PEAK"})
ENGINEERING_REVIEWED_QUALITY_FLAGS = frozenset({
    "AREA_ABOVE_MEDIAN",
    "LATTICE_RECOVERED_UNVERIFIED",
    "SATURATED_PEAK",
    "SUBPITCH_FRAGMENT_NEIGHBOR_REJECTED",
})


@dataclass(frozen=True)
class ExperimentalObservation:
    detection_id: int
    x: float
    y: float
    confidence: float


@dataclass(frozen=True)
class ExperimentalPair:
    task_id: str
    calibration: tuple[ExperimentalObservation, ...]
    measurement: tuple[ExperimentalObservation, ...]
    source_markers: tuple[str, ...] = ()


@lru_cache(maxsize=1)
def _validator() -> Draft202012Validator:
    schema = json.loads(SCHEMA_PATH.read_text(encoding="utf-8"))
    Draft202012Validator.check_schema(schema)
    return Draft202012Validator(schema)


def _parse_document(
    document: Mapping[str, object],
    image_type: str,
    engineering_mode: bool,
) -> tuple[tuple[ExperimentalObservation, ...], set[str]]:
    errors = sorted(_validator().iter_errors(document), key=lambda error: list(error.absolute_path))
    if errors:
        error = errors[0]
        path = ".".join(str(item) for item in error.absolute_path) or "root"
        raise ModelError(f"Invalid M2 experimental {image_type} document at {path}: {error.message}")
    if document["image_type"] != image_type:
        raise ModelError(f"Experimental input must use image_type={image_type!r}.")

    spots = document["spots"]
    if document["quality"]["detected_count"] != len(spots):
        raise ModelError("Experimental quality.detected_count must equal the spots array length.")

    observations: list[ExperimentalObservation] = []
    source_markers = {str(item) for item in document["quality"].get("warnings", [])}
    detection_ids: set[int] = set()
    for index, spot in enumerate(spots):
        detection_id = int(spot["detection_id"])
        if detection_id in detection_ids:
            raise ModelError(f"Experimental detection_id values must be unique within {image_type}.")
        detection_ids.add(detection_id)
        values = (float(spot["x"]), float(spot["y"]), float(spot["confidence"]))
        if not all(math.isfinite(value) for value in values):
            raise ModelError(f"Experimental spot {index} contains a non-finite numeric value.")
        flags = spot.get("quality_flags", [])
        source_markers.update(str(item) for item in flags)
        allowed_flags = (
            ENGINEERING_REVIEWED_QUALITY_FLAGS
            if engineering_mode
            else IDENTITY_SAFE_QUALITY_FLAGS
        )
        unsafe_flags = sorted(set(flags) - allowed_flags)
        if unsafe_flags:
            raise CoordinateSystemError(
                f"Experimental {image_type} detection {detection_id} has unsafe quality flags: {unsafe_flags}."
            )
        observations.append(
            ExperimentalObservation(
                detection_id=detection_id,
                x=values[0],
                y=values[1],
                confidence=values[2],
            )
        )
    return tuple(observations), source_markers


def parse_experimental_pair(
    calibration: Mapping[str, object],
    measurement: Mapping[str, object],
    engineering_mode: bool = False,
) -> ExperimentalPair:
    """Validate experimental inputs without assigning cross-image physical IDs."""

    calibration_observations, calibration_markers = _parse_document(
        calibration, "calibration", engineering_mode
    )
    measurement_observations, measurement_markers = _parse_document(
        measurement, "measurement", engineering_mode
    )
    if calibration["task_id"] != measurement["task_id"]:
        raise ModelError("Experimental calibration and measurement task_id values must match.")
    return ExperimentalPair(
        task_id=str(calibration["task_id"]),
        calibration=calibration_observations,
        measurement=measurement_observations,
        source_markers=tuple(sorted(calibration_markers | measurement_markers)),
    )
