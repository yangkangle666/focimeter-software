from pathlib import Path

from .errors import CONFIG_NOT_FOUND, IMAGE_NOT_FOUND, M1Failure


def canonical_root(project_root) -> Path:
    root = Path(project_root).expanduser().resolve(strict=False)
    if not root.is_dir():
        raise M1Failure("INPUT_INVALID", "project_root 必须是已存在的目录。")
    return root


def resolve_relative_file(root: Path, value, error_code: str, field: str) -> Path:
    candidate = Path(value)
    if candidate.is_absolute():
        raise M1Failure(error_code, f"{field} 不允许使用绝对路径。", {"missing_field": field, "path": str(value)})
    candidate = (root / candidate).resolve(strict=False)
    try:
        candidate.relative_to(root)
    except ValueError:
        raise M1Failure(error_code, f"{field} 必须位于项目根目录内。", {"missing_field": field, "path": str(value)})
    if not candidate.is_file():
        raise M1Failure(error_code, f"{field} 不存在: {value}", {"missing_field": field, "missing_path": str(value)})
    return candidate


def relative_path(root: Path, value: Path) -> str:
    return value.relative_to(root).as_posix()


def safe_task_id(task_id: str) -> bool:
    return bool(task_id) and len(task_id) <= 64 and all(ch.isascii() and (ch.isalnum() or ch in "_-") for ch in task_id)
