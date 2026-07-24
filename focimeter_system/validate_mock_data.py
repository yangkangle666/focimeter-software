from __future__ import annotations

import json
from pathlib import Path


ROOT = Path(__file__).resolve().parent
MOCK_DIR = ROOT / "data" / "mock"
CONFIG_PATH = ROOT / "config" / "default_config.json"
INPUT_PACKAGE_PATH = MOCK_DIR / "m1_input_config" / "input_package_ok.json"

EXPECTED_MOCK_INPUT_PATHS = {
    "calibration_image": "data/samples/calibration/calib_mock_001.jpg",
    "measurement_image": "data/samples/measurement/meas_mock_001.jpg",
    "config_path": "config/default_config.json",
}
MOCK_WARNING_PATHS = {
    INPUT_PACKAGE_PATH: ("quality", "warnings"),
    MOCK_DIR / "m2_image_recognition" / "spots_calib_ok.json": ("quality", "warnings"),
    MOCK_DIR / "m2_image_recognition" / "spots_meas_ok.json": ("quality", "warnings"),
    MOCK_DIR / "m3_calibration_calculation" / "result_spherical_ok.json": ("quality", "warnings"),
    MOCK_DIR / "m3_calibration_calculation" / "result_cylindrical_ok.json": ("quality", "warnings"),
    MOCK_DIR / "m4_local_system" / "display_output_ok.json": ("display", "warnings"),
}

REQUIRED_TOP_LEVEL_FIELDS = {"schema_version", "task_id", "module", "status"}
VALID_STATUS = {"ok", "error"}


def load_json(path: Path) -> dict:
    with path.open("r", encoding="utf-8") as file:
        data = json.load(file)
    if not isinstance(data, dict):
        raise ValueError(f"{path} must contain a JSON object")
    return data


def validate_common_fields(path: Path, data: dict) -> None:
    missing = REQUIRED_TOP_LEVEL_FIELDS - set(data)
    if missing:
        raise ValueError(f"{path} missing fields: {sorted(missing)}")

    if data["schema_version"] != "1.0":
        raise ValueError(f"{path} schema_version must be 1.0")

    if data["status"] not in VALID_STATUS:
        raise ValueError(f"{path} status must be one of {sorted(VALID_STATUS)}")

    if data["status"] == "error":
        error = data.get("error")
        if not isinstance(error, dict):
            raise ValueError(f"{path} error status must include error object")
        for field in ["code", "message", "module", "recoverable"]:
            if field not in error:
                raise ValueError(f"{path} error object missing field: {field}")


def validate_checked_mock_paths(data: dict) -> int:
    payload = data.get("data")
    quality = data.get("quality")
    if not isinstance(payload, dict) or not isinstance(quality, dict):
        raise ValueError(f"{INPUT_PACKAGE_PATH} must contain data and quality objects")
    if quality.get("paths_checked") is not True:
        raise ValueError(f"{INPUT_PACKAGE_PATH} must set paths_checked=true")
    warnings = quality.get("warnings")
    if not isinstance(warnings, list) or "MOCK_DATA_ONLY" not in warnings:
        raise ValueError(f"{INPUT_PACKAGE_PATH} must be marked MOCK_DATA_ONLY")

    checked = 0
    for field, expected_relative in EXPECTED_MOCK_INPUT_PATHS.items():
        relative = payload.get(field)
        if relative != expected_relative:
            raise ValueError(
                f"{INPUT_PACKAGE_PATH} {field} must be {expected_relative!r}; got {relative!r}"
            )
        if not isinstance(relative, str) or not relative.strip():
            raise ValueError(f"{INPUT_PACKAGE_PATH} {field} must be a non-empty path")
        resolved = (ROOT / relative).resolve()
        try:
            resolved.relative_to(ROOT.resolve())
        except ValueError as error:
            raise ValueError(f"{INPUT_PACKAGE_PATH} {field} escapes project root") from error
        if not resolved.is_file() or resolved.stat().st_size <= 0:
            raise ValueError(f"{INPUT_PACKAGE_PATH} {field} is not a non-empty file: {resolved}")
        if field in {"calibration_image", "measurement_image"}:
            with resolved.open("rb") as file:
                if file.read(3) != b"\xff\xd8\xff":
                    raise ValueError(f"{INPUT_PACKAGE_PATH} {field} is not a JPEG file: {resolved}")
        checked += 1
    return checked


def validate_mock_marker(path: Path, data: dict) -> None:
    warning_path = MOCK_WARNING_PATHS.get(path)
    if warning_path is None:
        return
    value: object = data
    for field in warning_path:
        if not isinstance(value, dict) or field not in value:
            raise ValueError(f"{path} missing MOCK_DATA_ONLY marker path: {'.'.join(warning_path)}")
        value = value[field]
    if not isinstance(value, list) or "MOCK_DATA_ONLY" not in value:
        raise ValueError(f"{path} must be marked MOCK_DATA_ONLY at {'.'.join(warning_path)}")
    if "m3_calibration_calculation" in str(path) and "software_verified" not in value:
        raise ValueError(f"{path} must be marked software_verified at {chr(46).join(warning_path)}")


def main() -> int:
    paths = sorted(MOCK_DIR.rglob("*.json")) + [CONFIG_PATH]
    checked = 0
    checked_input_paths = 0

    for path in paths:
        data = load_json(path)
        if path != CONFIG_PATH:
            validate_common_fields(path, data)
        validate_mock_marker(path, data)
        if path == INPUT_PACKAGE_PATH:
            checked_input_paths = validate_checked_mock_paths(data)
        checked += 1

    print(f"OK: validated {checked} JSON files")
    print(f"OK: paths_checked=true verified {checked_input_paths} MOCK_DATA_ONLY input files")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
