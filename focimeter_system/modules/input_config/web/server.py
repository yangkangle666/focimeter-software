import argparse
import json
import mimetypes
import socket
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, unquote, urlparse

from .app import MAX_UPLOAD_BYTES, WebApplication, WebError


MAX_JSON_BYTES = 2 * 1024 * 1024
STATIC_ROOT = Path(__file__).with_name("static")


class M1RequestHandler(BaseHTTPRequestHandler):
    server_version = "M1Web/1.0"

    @property
    def application(self):
        return self.server.web_application

    def do_GET(self):
        parsed = urlparse(self.path)
        try:
            if parsed.path == "/api/bootstrap":
                self._json(HTTPStatus.OK, self.application.bootstrap())
            elif parsed.path.startswith("/api/task/") and parsed.path.endswith("/bundle"):
                task_id = unquote(parsed.path[len("/api/task/"):-len("/bundle")].rstrip("/"))
                bundle = self.application.integration_bundle(task_id)
                self._download(bundle["data"], bundle["filename"])
            elif parsed.path.startswith("/api/task/"):
                task_id = unquote(parsed.path.removeprefix("/api/task/"))
                self._json(HTTPStatus.OK, self.application.task_result(task_id))
            elif parsed.path == "/api/file":
                relative = parse_qs(parsed.query).get("path", [""])[0]
                self._file(self.application.open_project_file(relative))
            elif parsed.path.startswith("/api/"):
                self._json_error(HTTPStatus.NOT_FOUND, "NOT_FOUND", "接口不存在。")
            else:
                self._static(parsed.path)
        except WebError as exc:
            self._json(exc.status, exc.as_dict())
        except Exception as exc:
            self._json_error(HTTPStatus.INTERNAL_SERVER_ERROR, "WEB_INTERNAL_ERROR", str(exc))

    def do_POST(self):
        parsed = urlparse(self.path)
        try:
            if parsed.path == "/api/upload":
                query = parse_qs(parsed.query)
                length = self._content_length(MAX_UPLOAD_BYTES)
                relative = self.application.save_upload(
                    query.get("kind", [""])[0],
                    query.get("task_id", [""])[0],
                    query.get("filename", [""])[0],
                    self.rfile,
                    length,
                )
                self._json(HTTPStatus.CREATED, {"status": "ok", "path": relative})
            elif parsed.path == "/api/run":
                length = self._content_length(MAX_JSON_BYTES)
                try:
                    payload = json.loads(self.rfile.read(length).decode("utf-8"))
                except (UnicodeDecodeError, json.JSONDecodeError) as exc:
                    raise WebError(f"请求不是有效 JSON：{exc}", code="INVALID_JSON") from exc
                self._json(HTTPStatus.OK, self.application.run(payload))
            else:
                self._json_error(HTTPStatus.NOT_FOUND, "NOT_FOUND", "接口不存在。")
        except WebError as exc:
            self._json(exc.status, exc.as_dict())
        except Exception as exc:
            self._json_error(HTTPStatus.INTERNAL_SERVER_ERROR, "WEB_INTERNAL_ERROR", str(exc))

    def _content_length(self, maximum):
        raw = self.headers.get("Content-Length")
        if raw is None:
            raise WebError("请求缺少 Content-Length。", status=411, code="LENGTH_REQUIRED")
        try:
            length = int(raw)
        except ValueError as exc:
            raise WebError("Content-Length 无效。") from exc
        if length < 0 or length > maximum:
            raise WebError("请求内容过大。", status=413, code="REQUEST_TOO_LARGE")
        return length

    def _static(self, url_path):
        relative = "index.html" if url_path in {"", "/"} else unquote(url_path.lstrip("/"))
        candidate = (STATIC_ROOT / relative).resolve(strict=False)
        try:
            candidate.relative_to(STATIC_ROOT.resolve())
        except ValueError as exc:
            raise WebError("静态资源路径无效。", status=403, code="PATH_FORBIDDEN") from exc
        if not candidate.is_file():
            raise WebError("页面不存在。", status=404, code="NOT_FOUND")
        self._file(candidate, cache="no-cache")

    def _file(self, path: Path, cache="private, max-age=60"):
        content_type = mimetypes.guess_type(path.name)[0] or "application/octet-stream"
        data = path.read_bytes()
        self.send_response(HTTPStatus.OK)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", cache)
        self.send_header("X-Content-Type-Options", "nosniff")
        self.end_headers()
        self.wfile.write(data)

    def _download(self, data: bytes, filename: str):
        self.send_response(HTTPStatus.OK)
        self.send_header("Content-Type", "application/zip")
        self.send_header("Content-Disposition", f'attachment; filename="{filename}"')
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.end_headers()
        self.wfile.write(data)

    def _json(self, status, value):
        data = (json.dumps(value, ensure_ascii=False) + "\n").encode("utf-8")
        self.send_response(int(status))
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.end_headers()
        self.wfile.write(data)

    def _json_error(self, status, code, message):
        self._json(status, {"status": "error", "error": {"code": code, "message": message}})

    def log_message(self, format, *args):
        print(f"[{self.log_date_time_string()}] {self.address_string()} {format % args}")


def make_server(project_root, host="127.0.0.1", port=8765):
    server = ThreadingHTTPServer((host, port), M1RequestHandler)
    server.web_application = WebApplication(project_root)
    return server


def main(argv=None):
    parser = argparse.ArgumentParser(description="M1 浏览器操作台")
    parser.add_argument("--project-root", default=str(Path(__file__).resolve().parents[3]))
    parser.add_argument("--host", default="127.0.0.1", help="局域网访问时使用 0.0.0.0")
    parser.add_argument("--port", type=int, default=8765)
    args = parser.parse_args(argv)
    server = make_server(args.project_root, args.host, args.port)
    print(f"M1 操作台已启动：http://localhost:{server.server_port}")
    if args.host == "0.0.0.0":
        try:
            print(f"局域网地址：http://{socket.gethostbyname(socket.gethostname())}:{server.server_port}")
        except OSError:
            pass
        print("仅在可信局域网中使用；当前版本不包含身份认证。")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nM1 操作台已停止。")
    finally:
        server.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
