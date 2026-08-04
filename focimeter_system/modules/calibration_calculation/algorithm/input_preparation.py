"""Dispatch supported M3 input contracts into one paired calculation shape."""

from __future__ import annotations

from dataclasses import dataclass
from typing import Mapping

from .experimental_input import EXPERIMENTAL_SCHEMA_VERSION, parse_experimental_pair
from .multispot_matching import MatchDiagnostics, match_experimental_multispot
from .types import CalibrationModel, ModelError


@dataclass(frozen=True)
class PreparedInputs:
    calibration: Mapping[str, object]
    measurement: Mapping[str, object]
    matching: MatchDiagnostics | None


def prepare_calculation_inputs(
    calibration: Mapping[str, object],
    measurement: Mapping[str, object],
    model: CalibrationModel,
) -> PreparedInputs:
    """Preserve v1 inputs or conservatively pair M2 experimental detections."""

    calibration_version = calibration.get("schema_version")
    measurement_version = measurement.get("schema_version")
    if calibration_version == measurement_version == "1.0":
        return PreparedInputs(calibration, measurement, None)
    if calibration_version == measurement_version == EXPERIMENTAL_SCHEMA_VERSION:
        matched = match_experimental_multispot(
            parse_experimental_pair(calibration, measurement),
            model.matching_limits,
        )
        return PreparedInputs(matched.calibration, matched.measurement, matched.diagnostics)
    raise ModelError(
        "Calibration and measurement schemas must both use v1 physical spot_id input "
        "or m2.multispot.experimental.1 detections."
    )
