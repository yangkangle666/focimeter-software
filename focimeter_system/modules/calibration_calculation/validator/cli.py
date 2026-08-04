"""Command-line interface for M3 contract validation."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Sequence

from .contract_validator import ValidationIssue, ValidationReport, validate_inputs, validate_result


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Validate M3 JSON interface contracts.")
    commands = parser.add_subparsers(dest="command", required=True)

    inputs = commands.add_parser("inputs", help="Validate M2 spot files and unified configuration.")
    inputs.add_argument("--calibration", required=True, type=Path)
    inputs.add_argument("--measurement", required=True, type=Path)
    inputs.add_argument("--config", required=True, type=Path)
    inputs.add_argument("--mode", choices=("contract", "calculation-ready"), default="contract")

    result = commands.add_parser("result", help="Validate an M3 success or error output.")
    result.add_argument("--file", required=True, type=Path)
    return parser


def _failure(path: Path, code: str, message: str) -> ValidationReport:
    return ValidationReport(False, (ValidationIssue(str(path), code, message),))


def _reject_nonstandard_number(value: str) -> None:
    raise ValueError(f"Non-standard JSON number: {value}")


def _load(path: Path, missing_code: str) -> tuple[dict | None, ValidationReport | None]:
    if not path.is_file():
        return None, _failure(path, missing_code, f"File not found: {path}")
    try:
        with path.open("r", encoding="utf-8") as file:
            data = json.load(file, parse_constant=_reject_nonstandard_number)
    except (json.JSONDecodeError, UnicodeDecodeError, ValueError) as error:
        return None, _failure(path, "CONFIG_INVALID", f"Invalid UTF-8 JSON: {error}")
    except OSError as error:
        return None, _failure(path, "UNKNOWN_ERROR", str(error))
    if not isinstance(data, dict):
        return None, _failure(path, "CONFIG_INVALID", "The JSON root must be an object.")
    return data, None


def _validate_inputs(args: argparse.Namespace) -> ValidationReport:
    calibration, failure = _load(args.calibration, "IMAGE_NOT_FOUND")
    if failure:
        return failure
    measurement, failure = _load(args.measurement, "IMAGE_NOT_FOUND")
    if failure:
        return failure
    config, failure = _load(args.config, "CONFIG_NOT_FOUND")
    if failure:
        return failure
    return validate_inputs(calibration, measurement, config, args.mode)


def _validate_result(args: argparse.Namespace) -> ValidationReport:
    result, failure = _load(args.file, "IMAGE_NOT_FOUND")
    return failure or validate_result(result)


def main(argv: Sequence[str] | None = None) -> int:
    args = _parser().parse_args(argv)
    try:
        report = _validate_inputs(args) if args.command == "inputs" else _validate_result(args)
    except Exception as error:
        report = _failure(Path("."), "UNKNOWN_ERROR", str(error))
    print(json.dumps(report.to_dict(), ensure_ascii=False, indent=2))
    return 0 if report.valid else 2


if __name__ == "__main__":
    raise SystemExit(main())
