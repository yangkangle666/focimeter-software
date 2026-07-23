"""Validate M3 JSON contracts without performing optical calculations."""

from __future__ import annotations

import json
import math
from dataclasses import asdict, dataclass
from functools import lru_cache
from pathlib import Path
from typing import Literal, Mapping

from jsonschema import Draft202012Validator
from jsonschema.exceptions import ValidationError


SCHEMA_ROOT = Path(__file__).resolve().parents[1] / "schemas"
REQUIRED_ROLES = ("center", "x_positive", "y_positive")
READY_PARAMETERS = (
    ("optical", "distance_m"),
)
UNIT_FIELDS = {
    "coordinate_type",
    "coordinate_type_before",
    "coordinate_type_after",
    "shift_unit",
    "unit",
    "angle_unit",
    "diopter_unit",
}


@dataclass(frozen=True)
class ValidationIssue:
    path: str
    code: str
    message: str


@dataclass(frozen=True)
class ValidationReport:
    valid: bool
    issues: tuple[ValidationIssue, ...]

    def to_dict(self) -> dict[str, object]:
        return {
            "valid": self.valid,
            "issues": [asdict(issue) for issue in self.issues],
        }


@lru_cache(maxsize=None)
def _validator(schema_name: str) -> Draft202012Validator:
    schema = json.loads((SCHEMA_ROOT / schema_name).read_text(encoding="utf-8"))
    Draft202012Validator.check_schema(schema)
    return Draft202012Validator(schema)


def _format_path(prefix: str, error: ValidationError) -> str:
    path = prefix
    for item in error.absolute_path:
        path += f"[{item}]" if isinstance(item, int) else f".{item}"
    return path


def _schema_code(path: str, default_code: str) -> str:
    segments = {part.replace("]", "") for part in path.replace("[", ".").split(".")}
    return "UNIT_MISMATCH" if segments & UNIT_FIELDS else default_code


def _schema_issues(
    data: Mapping[str, object],
    schema_name: str,
    prefix: str,
    default_code: str,
) -> list[ValidationIssue]:
    errors = sorted(
        _validator(schema_name).iter_errors(data),
        key=lambda error: (list(error.absolute_path), error.message),
    )
    return [
        ValidationIssue(
            path=(path := _format_path(prefix, error)),
            code=_schema_code(path, default_code),
            message=error.message,
        )
        for error in errors
    ]


def _report(issues: list[ValidationIssue]) -> ValidationReport:
    return ValidationReport(valid=not issues, issues=tuple(issues))


def _nonfinite_issues(value: object, path: str, code: str) -> list[ValidationIssue]:
    issues: list[ValidationIssue] = []
    if isinstance(value, float) and not math.isfinite(value):
        issues.append(ValidationIssue(path, code, "Numeric values must be finite JSON numbers."))
    elif isinstance(value, Mapping):
        for key, item in value.items():
            issues.extend(_nonfinite_issues(item, f"{path}.{key}", code))
    elif isinstance(value, list):
        for index, item in enumerate(value):
            issues.extend(_nonfinite_issues(item, f"{path}[{index}]", code))
    return issues


def _role_issues(document: Mapping[str, object], prefix: str) -> list[ValidationIssue]:
    spots = document["spots"]
    issues: list[ValidationIssue] = []
    for role in REQUIRED_ROLES:
        count = sum(spot["role"] == role for spot in spots)
        if count != 1:
            issues.append(
                ValidationIssue(
                    path=f"{prefix}.spots",
                    code="COORDINATE_SYSTEM_INVALID",
                    message=f"Role '{role}' must appear exactly once; found {count}.",
                )
            )
    return issues


def _pairing_role_issues(document: Mapping[str, object], prefix: str) -> list[ValidationIssue]:
    roles = [spot["role"] for spot in document["spots"]]
    if "unknown" in roles or len(set(roles)) != len(roles):
        return [
            ValidationIssue(
                path=f"{prefix}.spots",
                code="COORDINATE_SYSTEM_INVALID",
                message="Calculation requires unique, known spot roles.",
            )
        ]
    return []


def _spot_id_issues(document: Mapping[str, object], prefix: str) -> list[ValidationIssue]:
    spot_ids = [spot["spot_id"] for spot in document["spots"]]
    if len(spot_ids) == len(set(spot_ids)):
        return []
    return [
        ValidationIssue(
            path=f"{prefix}.spots",
            code="CONFIG_INVALID",
            message="spot_id values must be unique within each spot file.",
        )
    ]


def _spot_pairing_issues(
    calibration: Mapping[str, object],
    measurement: Mapping[str, object],
) -> list[ValidationIssue]:
    calibration_by_id = {spot["spot_id"]: spot for spot in calibration["spots"]}
    measurement_by_id = {spot["spot_id"]: spot for spot in measurement["spots"]}
    if set(calibration_by_id) != set(measurement_by_id):
        return [
            ValidationIssue(
                path="measurement.spots",
                code="COORDINATE_SYSTEM_INVALID",
                message="Calibration and measurement spot_id sets must match.",
            )
        ]
    mismatched_ids = [
        spot_id
        for spot_id in sorted(calibration_by_id)
        if calibration_by_id[spot_id]["role"] != measurement_by_id[spot_id]["role"]
    ]
    if mismatched_ids:
        return [
            ValidationIssue(
                path="measurement.spots",
                code="COORDINATE_SYSTEM_INVALID",
                message=f"spot_id values must preserve roles across inputs; mismatched IDs: {mismatched_ids}.",
            )
        ]
    return []


def _count_issues(
    document: Mapping[str, object],
    prefix: str,
    expected_count: int,
) -> list[ValidationIssue]:
    issues: list[ValidationIssue] = []
    spots = document["spots"]
    quality = document["quality"]
    if quality["detected_count"] != len(spots):
        issues.append(
            ValidationIssue(
                path=f"{prefix}.quality.detected_count",
                code="SPOT_COUNT_MISMATCH",
                message=f"detected_count is {quality['detected_count']} but spots contains {len(spots)} items.",
            )
        )
    if len(spots) != expected_count:
        issues.append(
            ValidationIssue(
                path=f"{prefix}.spots",
                code="SPOT_COUNT_MISMATCH",
                message=f"spots contains {len(spots)} items but config requires {expected_count}.",
            )
        )
    if quality["expected_count"] != expected_count:
        issues.append(
            ValidationIssue(
                path=f"{prefix}.quality.expected_count",
                code="SPOT_COUNT_MISMATCH",
                message=f"expected_count is {quality['expected_count']} but config requires {expected_count}.",
            )
        )
    return issues


def validate_inputs(
    calibration: Mapping[str, object],
    measurement: Mapping[str, object],
    config: Mapping[str, object],
    mode: Literal["contract", "calculation-ready"] = "contract",
) -> ValidationReport:
    """Validate M2 spot results and configuration for M3 consumption."""

    issues = _schema_issues(calibration, "spot_result.schema.json", "calibration", "CONFIG_INVALID")
    issues.extend(_schema_issues(measurement, "spot_result.schema.json", "measurement", "CONFIG_INVALID"))
    issues.extend(_schema_issues(config, "config.schema.json", "config", "CONFIG_INVALID"))
    issues.extend(_nonfinite_issues(calibration, "calibration", "CONFIG_INVALID"))
    issues.extend(_nonfinite_issues(measurement, "measurement", "CONFIG_INVALID"))
    issues.extend(_nonfinite_issues(config, "config", "CONFIG_INVALID"))
    if issues:
        return _report(issues)

    if mode not in ("contract", "calculation-ready"):
        return _report(
            [ValidationIssue("mode", "CONFIG_INVALID", "mode must be 'contract' or 'calculation-ready'.")]
        )

    if calibration["image_type"] != "calibration":
        issues.append(ValidationIssue("calibration.image_type", "CONFIG_INVALID", "Expected image_type 'calibration'."))
    if measurement["image_type"] != "measurement":
        issues.append(ValidationIssue("measurement.image_type", "CONFIG_INVALID", "Expected image_type 'measurement'."))
    if calibration["task_id"] != measurement["task_id"]:
        issues.append(ValidationIssue("measurement.task_id", "CONFIG_INVALID", "Input task_id values must match."))
    if calibration["schema_version"] != measurement["schema_version"]:
        issues.append(
            ValidationIssue("measurement.schema_version", "CONFIG_INVALID", "Input schema_version values must match.")
        )

    expected_count = config["recognition"]["expected_spot_count"]
    for document, prefix in ((calibration, "calibration"), (measurement, "measurement")):
        issues.extend(_spot_id_issues(document, prefix))
        issues.extend(_role_issues(document, prefix))
        issues.extend(_count_issues(document, prefix, expected_count))
        if not document["quality"]["is_usable"]:
            issues.append(
                ValidationIssue(
                    f"{prefix}.quality.is_usable",
                    "CALCULATION_FAILED",
                    "M3 requires usable M2 spot results.",
                )
            )

    if mode == "calculation-ready":
        for document, prefix in ((calibration, "calibration"), (measurement, "measurement")):
            issues.extend(_pairing_role_issues(document, prefix))
        issues.extend(_spot_pairing_issues(calibration, measurement))
        for section, field in READY_PARAMETERS:
            value = config[section][field]
            if (
                isinstance(value, bool)
                or not isinstance(value, (int, float))
                or not math.isfinite(value)
                or value <= 0
            ):
                issues.append(
                    ValidationIssue(
                        f"config.{section}.{field}",
                        "CONFIG_INVALID",
                        "A positive numeric hardware value is required for calculation-ready mode.",
                    )
                )

    return _report(issues)


def validate_result(result: Mapping[str, object]) -> ValidationReport:
    """Validate either a successful M3 result or a unified M3 error envelope."""

    if result.get("status") == "error":
        issues = _schema_issues(result, "error.schema.json", "result", "CONFIG_INVALID")
        issues.extend(_nonfinite_issues(result, "result", "CONFIG_INVALID"))
    else:
        issues = _schema_issues(result, "result_success.schema.json", "result", "CALCULATION_FAILED")
        issues.extend(_nonfinite_issues(result, "result", "CALCULATION_FAILED"))
    return _report(issues)
