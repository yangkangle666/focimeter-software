import argparse
import json
import sys
from pathlib import Path

from .service import run_m1


def main(argv=None):
    parser = argparse.ArgumentParser(description="M1 输入与配置模块")
    parser.add_argument("--input", required=True, help="request_ok.json 路径")
    parser.add_argument("--project-root", default=".")
    parser.add_argument("--output", help="可选：保存 input_package.json 或 error JSON")
    args = parser.parse_args(argv)
    try:
        request = json.loads(Path(args.input).read_text(encoding="utf-8-sig"))
        result = run_m1(request, Path(args.project_root))
    except (OSError, json.JSONDecodeError) as exc:
        result = {"schema_version": "1.0", "task_id": "", "module": "m1_input_config", "status": "error", "error": {"code": "INPUT_INVALID", "message": f"输入 JSON 无法读取: {exc}", "module": "m1_input_config", "recoverable": True, "details": {}}}
    text = json.dumps(result, ensure_ascii=False, indent=2) + "\n"
    print(text, end="")
    if args.output:
        output = Path(args.output)
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(text, encoding="utf-8")
    return 0 if result.get("status") == "ok" else 1


if __name__ == "__main__":
    sys.exit(main())
