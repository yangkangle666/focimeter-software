import io
import json
import os
import re
import tempfile
import zipfile
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
        calibration_files = self._list_files("calibration", IMAGE_EXTENSIONS)
        measurement_files = self._list_files("measurement", IMAGE_EXTENSIONS)
        synthetic_calibration = (
            "data/mock/m2_image_recognition/synthetic_multispot/"
            "calibration/94_clean_reference.png"
        )
        synthetic_measurement = (
            "data/mock/m2_image_recognition/synthetic_multispot/"
            "measurement/94_measured_local_deformation.png"
        )
        if (self.root / synthetic_calibration).is_file():
            calibration_files.append(synthetic_calibration)
        if (self.root / synthetic_measurement).is_file():
            measurement_files.append(synthetic_measurement)
        return {
            "files": {
                "calibration": sorted(set(calibration_files)),
                "measurement": sorted(set(measurement_files)),
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

    def integration_bundle(self, task_id: str):
        if not safe_task_id(task_id):
            raise WebError("任务编号无效。")

        package_path = self.root / "outputs/results" / task_id / "input_package.json"
        if not package_path.is_file():
            log_path = self.root / "outputs/logs" / f"{task_id}_input_config.json"
            if log_path.is_file():
                raise WebError("任务尚未生成可交付的输入包。", status=409, code="TASK_NOT_READY")
            raise WebError("没有找到该任务。", status=404, code="TASK_NOT_FOUND")

        try:
            package_bytes = package_path.read_bytes()
            package = json.loads(package_bytes.decode("utf-8-sig"))
        except (OSError, UnicodeDecodeError, json.JSONDecodeError) as exc:
            raise WebError(f"无法读取输入包：{exc}", status=500, code="BUNDLE_BUILD_FAILED") from exc

        if not isinstance(package, dict) or package.get("status") != "ok":
            raise WebError("任务输入包未成功生成，不能下载联调包。", status=409, code="TASK_NOT_READY")

        quality = package.get("quality")
        required_checks = ("paths_checked", "config_checked", "is_usable")
        if not isinstance(quality, dict) or any(quality.get(key) is not True for key in required_checks):
            raise WebError("输入包尚未通过路径和配置检查，不能下载联调包。", status=409, code="TASK_NOT_READY")

        data = package.get("data")
        if not isinstance(data, dict):
            raise WebError("输入包缺少 data 文件清单。", status=500, code="BUNDLE_BUILD_FAILED")
        config_relative = data.get("config_path")
        if not isinstance(config_relative, str) or not config_relative:
            raise WebError(
                "输入包缺少有效的 config_path 路径。",
                status=422,
                code="BUNDLE_FILE_MISSING",
            )
        config_path = self._resolve_bundle_file(config_relative)
        try:
            config = self._read_json(config_path)
        except (OSError, UnicodeDecodeError, json.JSONDecodeError) as exc:
            raise WebError(
                f"无法读取联调包配置文件：{exc}",
                status=422,
                code="BUNDLE_BUILD_FAILED",
            ) from exc

        references = [
            ("calibration_image", data.get("calibration_image")),
            ("measurement_image", data.get("measurement_image")),
            ("config_path", config_relative),
        ]
        calibration_reference = config.get("calibration_reference")
        if calibration_reference:
            references.append(
                ("calibration_file", calibration_reference.get("calibration_file"))
            )

        files = []
        for field, relative in references:
            if not isinstance(relative, str) or not relative:
                raise WebError(
                    f"输入包缺少有效的 {field} 路径。",
                    status=422,
                    code="BUNDLE_FILE_MISSING",
                )
            files.append((field, relative, self._resolve_bundle_file(relative)))

        readme = self._integration_readme(task_id, config)
        output = io.BytesIO()
        try:
            with zipfile.ZipFile(output, "w", compression=zipfile.ZIP_DEFLATED) as archive:
                archive.writestr("input_package.json", package_bytes)
                written = {"input_package.json"}
                for _, relative, path in files:
                    if relative not in written:
                        archive.write(path, arcname=relative)
                        written.add(relative)
                archive.writestr("README_M1_M2_INTEGRATION.md", readme)
        except OSError as exc:
            raise WebError(f"联调包生成失败：{exc}", status=500, code="BUNDLE_BUILD_FAILED") from exc

        return {
            "filename": f"m1_{task_id}_m2_integration_bundle.zip",
            "data": output.getvalue(),
        }

    def _resolve_bundle_file(self, relative: str) -> Path:
        value = Path(relative)
        if (
            value.is_absolute()
            or value.drive
            or not value.parts
            or value.parts[0] not in ALLOWED_FILE_ROOTS
            or ".." in value.parts
            or "\\" in relative
        ):
            raise WebError(
                f"联调包文件路径不允许：{relative}",
                status=403,
                code="PATH_FORBIDDEN",
            )

        candidate = (self.root / value).resolve(strict=False)
        try:
            candidate.relative_to(self.root)
        except ValueError as exc:
            raise WebError(
                f"联调包文件必须位于项目目录内：{relative}",
                status=403,
                code="PATH_FORBIDDEN",
            ) from exc
        if not candidate.is_file():
            raise WebError(
                f"联调包引用的文件不存在：{relative}",
                status=422,
                code="BUNDLE_FILE_MISSING",
            )
        return candidate

    @staticmethod
    def _integration_readme(task_id: str, config: Mapping) -> str:
        profile = config.get("data_profile") or {}
        calibration_reference = config.get("calibration_reference") or {}
        data_source = profile.get("data_source", "legacy")
        validation_status = profile.get("validation_status", "undeclared")
        hardware_confirmed = profile.get("hardware_parameters_confirmed", False)
        calibration_version = calibration_reference.get("calibration_version", "undeclared")
        return (
            "# M1 -> M2 软件联调包\n\n"
            f"任务编号：`{task_id}`\n\n"
            "## 配置状态\n\n"
            f"- 数据来源：`{data_source}`\n"
            f"- 验证状态：`{validation_status}`\n"
            f"- 硬件参数已确认：`{str(bool(hardware_confirmed)).lower()}`\n"
            f"- 标定版本：`{calibration_version}`\n\n"
            "## 使用步骤\n\n"
            "1. 解压本 ZIP 文件，并将解压目录作为 M2 的 `project_root`。\n"
            "2. 读取根目录的 `input_package.json`。\n"
            "3. 按 JSON 中 `data` 的相对路径读取标定图、测量图和配置文件。\n"
            "4. 配置中的 `calibration_reference.calibration_file` 已随包提供。\n\n"
            "## 检查结论\n\n"
            "M1 已检查所有声明路径、主配置和标定配置，并在下载前再次确认文件存在且位于项目目录内。\n\n"
            "> 重要：本包表示软件联调可用，不代表真实计量验证完成。\n"
        )

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
