import json
import os
import re
import tempfile
from pathlib import Path
from typing import BinaryIO, Mapping

from ..errors import TASK_CONFLICT, M1Failure
from ..paths import safe_task_id
from ..service import run_m1


MAX_UPLOAD_BYTES = 32 * 1024 * 1024
IMAGE_EXTENSIONS = {".png", ".jpg", ".jpeg", ".bmp", ".tif", ".tiff"}
ALLOWED_FILE_ROOTS = {"data", "config", "outputs"}


class WebError(Exception):
    def __init__(self, message: str, status: int = 400, code: str = "WEB_INPUT_INVALID"):
        super().__init__(message)
        self.message = message
        self.status = status
        self.code = code

    def as_dict(self):
        return {"status": "error", "error": {"code": self.code, "message": self.message}}


class WebApplication:
    def __init__(self, project_root):
        self.root = Path(project_root).expanduser().resolve(strict=True)

    def bootstrap(self):
        return {
            "files": {
                "calibration": self._list_files("calibration", IMAGE_EXTENSIONS),
                "measurement": self._list_files("measurement", IMAGE_EXTENSIONS),
                "config": self._list_configs(),
            },
            "default_config": self._read_json(self.root / "config/default_config.json"),
            "recent_tasks": self._recent_tasks(),
            "limits": {"upload_bytes": MAX_UPLOAD_BYTES},
        }

    def save_upload(self, kind: str, task_id: str, filename: str, stream: BinaryIO, length: int):
        if kind not in {"calibration", "measurement", "config"}:
            raise WebError("上传类型无效。")
        if not safe_task_id(task_id):
            raise WebError("任务编号只能包含字母、数字、下划线和短横线，最长 64 个字符。")
        if length < 0 or length > MAX_UPLOAD_BYTES:
            raise WebError("单个上传文件不能超过 32 MiB。", status=413, code="UPLOAD_TOO_LARGE")
        if not filename or Path(filename).name != filename or "/" in filename or "\\" in filename:
            raise WebError("文件名无效。")

        suffix = Path(filename).suffix
        allowed = {".json"} if kind == "config" else IMAGE_EXTENSIONS
        if suffix.lower() not in allowed:
            raise WebError("不支持该文件类型。")
        safe_name = self._safe_filename(filename)
        target_dir = self.root / "data/uploads" / task_id / kind
        target_dir.mkdir(parents=True, exist_ok=True)
        target = target_dir / safe_name

        data = stream.read(length)
        if len(data) != length:
            raise WebError("上传内容长度不正确或超过 32 MiB。", status=413, code="UPLOAD_TOO_LARGE")
        if kind == "config":
            try:
                json.loads(data.decode("utf-8-sig"))
            except (UnicodeDecodeError, json.JSONDecodeError) as exc:
                raise WebError(f"配置文件不是有效 JSON：{exc}") from exc
        self._atomic_bytes(target, data)
        return target.relative_to(self.root).as_posix()

    def run(self, payload: Mapping):
        if not isinstance(payload, Mapping):
            raise WebError("运行请求必须是 JSON 对象。")
        task_id = payload.get("task_id", "")
        if not isinstance(task_id, str) or not safe_task_id(task_id):
            raise WebError("请输入有效且唯一的任务编号。")

        package_path = self.root / "outputs/results" / task_id / "input_package.json"
        log_path = self.root / "outputs/logs" / f"{task_id}_input_config.json"
        if package_path.exists() or log_path.exists():
            failure = M1Failure(TASK_CONFLICT, "任务编号已存在，请更换任务编号后重试。")
            result = {
                "schema_version": "1.0", "task_id": task_id, "module": "m1_input_config",
                "status": "error", "error": failure.as_dict(),
            }
            return {"result": result, "log": self._read_json(log_path) if log_path.is_file() else None}

        config_data = payload.get("config_data")
        if config_data is not None:
            config_path = self.root / "data/uploads" / task_id / "config/config.json"
            self._atomic_json(config_path, config_data)
            config_relative = config_path.relative_to(self.root).as_posix()
        else:
            config_relative = payload.get("config_path", "")

        request = {
            "schema_version": "1.0",
            "task_id": task_id,
            "module": "m1_input_config",
            "status": "ok",
            "request": {
                "calibration_image": payload.get("calibration_image", ""),
                "measurement_image": payload.get("measurement_image", ""),
                "config_path": config_relative,
                "run_mode": "local_image",
                "operator": payload.get("operator", ""),
                "notes": payload.get("notes", ""),
            },
            "error": None,
        }
        result = run_m1(request, self.root)
        log = self._read_json(log_path) if log_path.is_file() else None
        return {"result": result, "log": log}

    def task_result(self, task_id: str):
        if not safe_task_id(task_id):
            raise WebError("任务编号无效。")
        package = self.root / "outputs/results" / task_id / "input_package.json"
        log = self.root / "outputs/logs" / f"{task_id}_input_config.json"
        if not package.is_file() and not log.is_file():
            raise WebError("没有找到该任务。", status=404, code="TASK_NOT_FOUND")
        return {
            "result": self._read_json(package) if package.is_file() else None,
            "log": self._read_json(log) if log.is_file() else None,
        }

    def open_project_file(self, relative: str) -> Path:
        value = Path(relative)
        if value.is_absolute() or not value.parts or value.parts[0] not in ALLOWED_FILE_ROOTS:
            raise WebError("文件必须位于项目目录允许的区域内。", status=403, code="PATH_FORBIDDEN")
        candidate = (self.root / value).resolve(strict=False)
        try:
            candidate.relative_to(self.root)
        except ValueError as exc:
            raise WebError("文件必须位于项目目录内。", status=403, code="PATH_FORBIDDEN") from exc
        if not candidate.is_file():
            raise WebError("文件不存在。", status=404, code="FILE_NOT_FOUND")
        return candidate

    def _list_files(self, kind: str, extensions):
        candidates = []
        roots = [self.root / "data/samples" / kind, self.root / "data/uploads"]
        for base in roots:
            if not base.is_dir():
                continue
            for path in base.rglob("*"):
                if path.is_file() and path.suffix.lower() in extensions:
                    if base.name == "uploads" and kind not in path.parts:
                        continue
                    candidates.append(path.relative_to(self.root).as_posix())
        return sorted(set(candidates))

    def _list_configs(self):
        values = []
        for base in (self.root / "config", self.root / "data/uploads"):
            if base.is_dir():
                values.extend(
                    path.relative_to(self.root).as_posix()
                    for path in base.rglob("*.json")
                    if path.is_file() and (base.name != "uploads" or "config" in path.parts)
                )
        return sorted(set(values))

    def _recent_tasks(self):
        base = self.root / "outputs/results"
        if not base.is_dir():
            return []
        packages = sorted(base.glob("*/input_package.json"), key=lambda path: path.stat().st_mtime, reverse=True)
        return [self._read_json(path) for path in packages[:8]]

    @staticmethod
    def _safe_filename(filename: str):
        stem = re.sub(r"[^A-Za-z0-9_-]+", "_", Path(filename).stem).strip("_") or "upload"
        return stem[:80] + Path(filename).suffix

    @staticmethod
    def _read_json(path: Path):
        return json.loads(path.read_text(encoding="utf-8-sig"))

    @staticmethod
    def _atomic_bytes(path: Path, data: bytes):
        fd, temporary = tempfile.mkstemp(prefix=f".{path.name}.", suffix=".tmp", dir=path.parent)
        try:
            with os.fdopen(fd, "wb") as stream:
                stream.write(data)
                stream.flush()
                os.fsync(stream.fileno())
            os.replace(temporary, path)
        finally:
            try:
                os.unlink(temporary)
            except FileNotFoundError:
                pass

    def _atomic_json(self, path: Path, value):
        path.parent.mkdir(parents=True, exist_ok=True)
        data = (json.dumps(value, ensure_ascii=False, indent=2) + "\n").encode("utf-8")
        self._atomic_bytes(path, data)
