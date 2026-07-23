"""Command-line tools for building calibration parameters and running M3."""

from __future__ import annotations

import argparse
import json
import os
import tempfile
from pathlib import Path
from typing import Sequence

from .calibration import fit_calibration_model
from .calculator import _error, calculate
from .types import CalibrationDataError, CalibrationModel, ModelError


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Build calibration parameters and run the M3 focimeter calculation algorithm."
    )
    commands = parser.add_subparsers(dest="command", required=True)

    fit = commands.add_parser(
        "fit-model",
        help="Build a calibration parameter artifact from standard-lens spot data.",
    )
    fit.add_argument("--dataset", required=True, type=Path)
    fit.add_argument("--config", required=True, type=Path)
    fit.add_argument("--project-root", required=True, type=Path)
    fit.add_argument("--output", required=True, type=Path)

    run = commands.add_parser("calculate", help="Calculate S/C/A from M2 spot outputs.")
    run.add_argument("--calibration", required=True, type=Path)
    run.add_argument("--measurement", required=True, type=Path)
    run.add_argument("--config", required=True, type=Path)
    run.add_argument(
        "--model",
        type=Path,
        default=Path("modules/calibration_calculation/calibration_model.json"),
    )
    run.add_argument("--allow-simulation-model", action="store_true")
    return parser


def _reject_nonstandard_number(value: str) -> None:
    raise ValueError(f"Non-standard JSON number: {value}")


def _read_json(path: Path) -> dict[str, object]:
    with path.open("r", encoding="utf-8") as file:
        data = json.load(file, parse_constant=_reject_nonstandard_number)
    if not isinstance(data, dict):
        raise ValueError(f"JSON root must be an object: {path}")
    return data


def _atomic_write_json(path: Path, data: dict[str, object]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary_name = tempfile.mkstemp(prefix=f".{path.name}.", suffix=".tmp", dir=path.parent)
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8", newline="\n") as file:
            json.dump(data, file, ensure_ascii=False, indent=2, allow_nan=False)
            file.write("\n")
        os.replace(temporary_name, path)
    except Exception:
        try:
            os.unlink(temporary_name)
        except FileNotFoundError:
            pass
        raise


def _fit(args: argparse.Namespace) -> tuple[dict[str, object], int]:
    try:
        dataset = _read_json(args.dataset)
        config = _read_json(args.config)
        model = fit_calibration_model(dataset, args.project_root, config)
        _atomic_write_json(args.output, model.to_dict())
        return {
            "status": "ok",
            "model_id": model.model_id,
            "validation_status": model.validation_status,
            "output": str(args.output),
        }, 0
    except (OSError, ValueError, CalibrationDataError, ModelError) as error:
        return {"status": "error", "error": {"code": "CONFIG_INVALID", "message": str(error)}}, 2


def _calculate(args: argparse.Namespace) -> tuple[dict[str, object], int]:
    task_id = "unknown_task"
    try:
        calibration = _read_json(args.calibration)
        task_id = str(calibration.get("task_id", task_id))
        measurement = _read_json(args.measurement)
        config = _read_json(args.config)
        model = CalibrationModel.from_dict(_read_json(args.model))
    except FileNotFoundError as error:
        missing = Path(error.filename) if error.filename else Path("unknown")
        code = "IMAGE_NOT_FOUND" if missing in {args.calibration, args.measurement} else "CONFIG_NOT_FOUND"
        result = _error(task_id, code, str(error), True, "FILE_NOT_FOUND", path=str(missing))
        return result, 2
    except (OSError, ValueError, json.JSONDecodeError, ModelError) as error:
        result = _error(task_id, "CONFIG_INVALID", str(error), False, "JSON_OR_MODEL_INVALID")
        return result, 2
    result = calculate(
        calibration,
        measurement,
        config,
        model,
        allow_simulation_model=args.allow_simulation_model,
    )
    return result, 0 if result["status"] == "ok" else 2


def main(argv: Sequence[str] | None = None) -> int:
    args = _parser().parse_args(argv)
    payload, exit_code = _fit(args) if args.command == "fit-model" else _calculate(args)
    print(json.dumps(payload, ensure_ascii=False, indent=2, allow_nan=False))
    return exit_code


if __name__ == "__main__":
    raise SystemExit(main())
