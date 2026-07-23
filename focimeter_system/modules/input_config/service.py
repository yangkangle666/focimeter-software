import hashlib
import json
import os
import stat
import tempfile
from datetime import datetime, timezone, timedelta
from pathlib import Path
from typing import Any, Mapping, Optional

from .errors import CONFIG_INVALID, CONFIG_NOT_FOUND, IMAGE_NOT_FOUND, INPUT_INVALID, M1Failure, TASK_CONFLICT, UNKNOWN_ERROR
from .paths import canonical_root, relative_path, resolve_relative_file, safe_task_id
from .validation import validate_config


REQUEST_FIELDS = {"schema_version", "task_id", "module", "status", "request", "error"}
REQUEST_DATA_FIELDS = {"calibration_image", "measurement_image", "config_path", "run_mode", "operator", "notes"}
MODULE = "m1_input_config"
SHANGHAI = timezone(timedelta(hours=8))
SOFTWARE_INTEGRATION_WARNING = (
    "SOFTWARE_INTEGRATION_ONLY: 仅表示路径和 JSON 契约可用于软件联调，不代表真实计量验证完成。"
)


class TaskLock:
    def __init__(self, path: Path):
        self.path = path
        self.fd = None

    def __enter__(self):
        self.path.parent.mkdir(parents=True, exist_ok=True)
        try:
            self.fd = os.open(self.path, os.O_CREAT | os.O_EXCL | os.O_WRONLY)
            os.write(self.fd, str(os.getpid()).encode("ascii"))
            return self
        except FileExistsError:
            raise M1Failure(TASK_CONFLICT, "任务正在运行。", {"lock_path": self.path.as_posix()})

    def __exit__(self, exc_type, exc, tb):
        if self.fd is not None:
            os.close(self.fd)
        try:
            self.path.unlink()
        except FileNotFoundError:
            pass


def _now():
    return datetime.now(SHANGHAI).isoformat(timespec="seconds")


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _read_json(path: Path):
    try:
        return json.loads(path.read_text(encoding="utf-8-sig"))
    except FileNotFoundError:
        raise
    except json.JSONDecodeError as exc:
        raise M1Failure(CONFIG_INVALID, f"JSON 格式错误: {exc.msg}")


def _atomic_json(path: Path, value: Any):
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(prefix=f".{path.name}.", suffix=".tmp", dir=path.parent)
    try:
        with os.fdopen(fd, "w", encoding="utf-8", newline="\n") as stream:
            json.dump(value, stream, ensure_ascii=False, indent=2)
            stream.write("\n")
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        try:
            os.unlink(temporary)
        except FileNotFoundError:
            pass


def _set_read_only(path: Path):
    path.chmod(stat.S_IRUSR | stat.S_IRGRP | stat.S_IROTH)


def _request_failure(request: Mapping[str, Any], root: Optional[Path], failure: M1Failure):
    result = {
        "schema_version": "1.0",
        "task_id": request.get("task_id", "") if isinstance(request, Mapping) else "",
        "module": MODULE,
        "status": "error",
        "error": failure.as_dict(),
    }
    if root and root.is_dir():
        try:
            log_name = f"{result['task_id']}_input_config.json" if safe_task_id(result["task_id"]) else f"m1_error_{os.getpid()}.json"
            _atomic_json(root / "outputs/logs" / log_name, result)
        except Exception:
            pass
    return result


def _validate_request(request: Mapping[str, Any]):
    if not isinstance(request, Mapping) or set(request) != REQUEST_FIELDS:
        raise M1Failure(INPUT_INVALID, "请求文件必须符合 request_ok.json 的统一外层格式。")
    if request["schema_version"] != "1.0" or request["module"] != MODULE or request["status"] != "ok" or request["error"] is not None:
        raise M1Failure(INPUT_INVALID, "请求的 schema_version、module、status 或 error 不符合 M1 契约。")
    if not isinstance(request["task_id"], str) or not safe_task_id(request["task_id"]):
        raise M1Failure(INPUT_INVALID, "task_id 必须是最多 64 个字符的 ASCII 字母、数字、_ 或 -。")
    if not isinstance(request["request"], Mapping) or not set(request["request"]).issubset(REQUEST_DATA_FIELDS):
        raise M1Failure(INPUT_INVALID, "request 内含未知字段。")
    data = request["request"]
    for field in ("calibration_image", "measurement_image", "config_path", "run_mode"):
        if field not in data:
            raise M1Failure(INPUT_INVALID, f"request 缺少字段: {field}", {"missing_field": field})
    if data["run_mode"] != "local_image":
        raise M1Failure(INPUT_INVALID, "M1 目前只支持 run_mode=local_image。")


def build_input_package(request: Mapping[str, Any], project_root) -> dict:
    root = canonical_root(project_root)
    _validate_request(request)
    data = request["request"]
    calibration = resolve_relative_file(root, data["calibration_image"], IMAGE_NOT_FOUND, "calibration_image")
    measurement = resolve_relative_file(root, data["measurement_image"], IMAGE_NOT_FOUND, "measurement_image")
    config_path = resolve_relative_file(root, data["config_path"], CONFIG_NOT_FOUND, "config_path")
    config = _read_json(config_path)
    warnings, error = validate_config(config)
    if error:
        raise error
    warnings.append(SOFTWARE_INTEGRATION_WARNING)

    task_id = request["task_id"]
    result_dir = root / "outputs/results" / task_id
    package_path = result_dir / "input_package.json"
    log_path = root / "outputs/logs" / f"{task_id}_input_config.json"
    hashes = {"calibration_image": _sha256(calibration), "measurement_image": _sha256(measurement), "config": _sha256(config_path)}
    if package_path.exists() or log_path.exists():
        raise M1Failure(TASK_CONFLICT, "任务编号已存在，不能覆盖已有 input_package.json。")

    with TaskLock(result_dir / ".m1.lock"):
        data_output = {
            "calibration_image": relative_path(root, calibration),
            "measurement_image": relative_path(root, measurement),
            "config_path": relative_path(root, config_path),
            "run_mode": data["run_mode"],
            "created_at": _now(),
        }
        output = {
            "schema_version": "1.0",
            "task_id": task_id,
            "module": MODULE,
            "status": "ok",
            "data": data_output,
            "quality": {"paths_checked": True, "config_checked": True, "is_usable": True, "warnings": warnings},
            "error": None,
        }
        _atomic_json(package_path, output)
        log = {
            "schema_version": "1.0", "task_id": task_id, "module": MODULE, "status": "ok",
            "input_files": [data_output["calibration_image"], data_output["measurement_image"], data_output["config_path"]],
            "output_files": [relative_path(root, package_path), relative_path(root, log_path)],
            "warnings": warnings, "sha256": hashes, "error": None,
        }
        _atomic_json(log_path, log)
        return output


def run_m1(request: Mapping[str, Any], project_root) -> dict:
    try:
        return build_input_package(request, project_root)
    except M1Failure as failure:
        root = Path(project_root).resolve(strict=False) if project_root else None
        return _request_failure(request, root if root and root.is_dir() else None, failure)
    except Exception as exc:
        root = Path(project_root).resolve(strict=False) if project_root else None
        return _request_failure(request, root if root and root.is_dir() else None, M1Failure(UNKNOWN_ERROR, str(exc), recoverable=False))
