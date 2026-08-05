"""Run the rendered-image M1-to-M3 software integration scenario."""

from __future__ import annotations

import argparse
from datetime import datetime
import json
import os
from pathlib import Path
import subprocess
import tempfile

from modules.calibration_calculation.algorithm.calculator import calculate
from modules.calibration_calculation.validator.contract_validator import validate_result
from modules.input_config.service import run_m1


ROOT = Path(__file__).resolve().parent
DATASET_ROOT = ROOT / "data" / "mock" / "m2_image_recognition" / "synthetic_multispot"
MODEL_PATH = (
    ROOT
    / "modules"
    / "calibration_calculation"
    / "examples"
    / "calibration"
    / "calibration_model.image_pipeline_simulation.json"
)
CASE_ID = "94_known_prescription"


def read_json(path: Path) -> dict:
    with path.open("r", encoding="utf-8") as stream:
        value = json.load(stream)
    if not isinstance(value, dict):
        raise ValueError(f"JSON root must be an object: {path}")
    return value


def write_json(path: Path, value: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary_name = tempfile.mkstemp(
        prefix=f".{path.name}.", suffix=".tmp", dir=path.parent
    )
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8", newline="\n") as stream:
            json.dump(value, stream, ensure_ascii=False, indent=2, allow_nan=False)
            stream.write("\n")
        os.replace(temporary_name, path)
    except Exception:
        try:
            os.unlink(temporary_name)
        except FileNotFoundError:
            pass
        raise


def relative_to_root(path: Path) -> str:
    return path.relative_to(ROOT).as_posix()


def find_case() -> dict:
    manifest = read_json(DATASET_ROOT / "manifest.json")
    matches = [item for item in manifest["cases"] if item["id"] == CASE_ID]
    if len(matches) != 1:
        raise ValueError(f"Synthetic manifest must contain exactly one {CASE_ID} case.")
    return matches[0]


def run(task_id: str, m2_executable: Path, save_intermediate: bool) -> tuple[dict, Path]:
    case = find_case()
    request = {
        "schema_version": "1.0",
        "task_id": task_id,
        "module": "m1_input_config",
        "status": "ok",
        "request": {
            "calibration_image": relative_to_root(DATASET_ROOT / case["images"]["calibration"]),
            "measurement_image": relative_to_root(DATASET_ROOT / case["images"]["measurement"]),
            "config_path": "config/default_config.json",
            "run_mode": "local_image",
            "operator": "synthetic_e2e_runner",
            "notes": "Rendered-image software integration; not metrology validation.",
        },
        "error": None,
    }
    m1_result = run_m1(request, ROOT)
    if m1_result.get("status") != "ok":
        raise RuntimeError(f"M1 failed: {json.dumps(m1_result, ensure_ascii=False)}")

    task_root = ROOT / "outputs" / "results" / task_id
    m2_output = task_root / "m2"
    command = [
        str(m2_executable),
        "--input",
        str(task_root / "input_package.json"),
        "--output",
        str(m2_output),
        "--project-root",
        str(ROOT),
        "--experimental-multispot",
    ]
    if save_intermediate:
        command.append("--save-intermediate")
    completed = subprocess.run(command, check=False, text=True, capture_output=True)
    if completed.returncode != 0:
        raise RuntimeError(
            f"M2 failed with exit code {completed.returncode}:\n{completed.stdout}\n{completed.stderr}"
        )

    experimental = m2_output / "experimental_multispot"
    result = calculate(
        read_json(experimental / "spots_calib_multispot.json"),
        read_json(experimental / "spots_meas_multispot.json"),
        read_json(ROOT / "config" / "default_config.json"),
        read_json(MODEL_PATH),
        allow_simulation_model=True,
    )
    result_path = task_root / "result.json"
    write_json(result_path, result)

    target = case["transform"]["prescription"]
    if result.get("status") != "ok" or not validate_result(result).valid:
        raise RuntimeError(f"M3 failed: {json.dumps(result, ensure_ascii=False)}")
    actual = result["result"]
    errors = {
        "S": abs(float(actual["S"]) - float(target["S"])),
        "C": abs(float(actual["C"]) - float(target["C"])),
        "A": abs(float(actual["A"]) - float(target["A"])),
    }
    if errors["S"] > 0.01 or errors["C"] > 0.01 or errors["A"] > 0.1:
        raise RuntimeError(f"M3 result exceeds synthetic tolerances: {errors}")
    return result, result_path


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--m2-executable", required=True, type=Path)
    parser.add_argument(
        "--task-id",
        default=f"synthetic_e2e_{datetime.now().strftime('%Y%m%d_%H%M%S')}",
    )
    parser.add_argument("--save-intermediate", action="store_true")
    args = parser.parse_args()

    try:
        result, result_path = run(args.task_id, args.m2_executable, args.save_intermediate)
    except (OSError, ValueError, RuntimeError) as error:
        print(json.dumps({"status": "error", "message": str(error)}, ensure_ascii=False, indent=2))
        return 1

    print(
        json.dumps(
            {
                "status": "ok",
                "task_id": args.task_id,
                "result": result["result"],
                "matched_spot_count": result["quality"]["matched_spot_count"],
                "fit_rmse": result["quality"]["fit_rmse"],
                "output": str(result_path),
                "scope": "software_integration_only",
            },
            ensure_ascii=False,
            indent=2,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
