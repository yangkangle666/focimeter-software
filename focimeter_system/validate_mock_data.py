from __future__ import annotations

import json
from pathlib import Path


ROOT = Path(__file__).resolve().parent
MOCK_DIR = ROOT / "data" / "mock"
CONFIG_PATH = ROOT / "config" / "default_config.json"

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


def main() -> int:
    paths = sorted(MOCK_DIR.rglob("*.json")) + [CONFIG_PATH]
    checked = 0

    for path in paths:
        data = load_json(path)
        if path != CONFIG_PATH:
            validate_common_fields(path, data)
        checked += 1

    print(f"OK: validated {checked} JSON files")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
