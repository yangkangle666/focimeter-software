import json
import tempfile
import threading
import unittest
import zipfile
from io import BytesIO
from pathlib import Path
from urllib.error import HTTPError
from urllib.parse import quote
from urllib.request import Request, urlopen

from modules.input_config.web.app import WebApplication, WebError
from modules.input_config.web.server import make_server


PROJECT_ROOT = Path(__file__).resolve().parents[3]


class WebApplicationTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        (self.root / "data/samples/calibration").mkdir(parents=True)
        (self.root / "data/samples/measurement").mkdir(parents=True)
        (self.root / "config").mkdir()
        (self.root / "data/samples/calibration/reference.png").write_bytes(b"calibration")
        (self.root / "data/samples/measurement/lens.png").write_bytes(b"measurement")
        source = PROJECT_ROOT / "config/default_config.json"
        (self.root / "config/default_config.json").write_text(
            source.read_text(encoding="utf-8"), encoding="utf-8"
        )
        self.app = WebApplication(self.root)

    def tearDown(self):
        self.temp.cleanup()

    def test_bootstrap_lists_existing_files_and_default_config(self):
        data = self.app.bootstrap()

        self.assertIn("data/samples/calibration/reference.png", data["files"]["calibration"])
        self.assertIn("data/samples/measurement/lens.png", data["files"]["measurement"])
        self.assertEqual(data["default_config"]["schema_version"], "1.0")

    def test_upload_writes_safe_project_relative_file(self):
        relative = self.app.save_upload(
            "measurement", "web_case_001", "lens image.PNG", BytesIO(b"image-data"), 10
        )

        self.assertEqual(relative, "data/uploads/web_case_001/measurement/lens_image.PNG")
        self.assertEqual((self.root / relative).read_bytes(), b"image-data")

    def test_upload_rejects_unsupported_extension(self):
        with self.assertRaisesRegex(WebError, "不支持"):
            self.app.save_upload("measurement", "web_case_001", "payload.exe", BytesIO(b"x"), 1)

    def test_project_file_rejects_path_traversal(self):
        with self.assertRaisesRegex(WebError, "项目目录"):
            self.app.open_project_file("../secret.txt")

    def test_run_saves_edited_config_and_returns_log(self):
        config = json.loads((self.root / "config/default_config.json").read_text(encoding="utf-8"))
        config["camera"]["image_width"] = 1024
        payload = {
            "task_id": "web_case_001",
            "operator": "tester",
            "notes": "web flow",
            "calibration_image": "data/samples/calibration/reference.png",
            "measurement_image": "data/samples/measurement/lens.png",
            "config_data": config,
        }

        response = self.app.run(payload)

        self.assertEqual(response["result"]["status"], "ok")
        self.assertEqual(response["result"]["data"]["config_path"], "data/uploads/web_case_001/config/config.json")
        self.assertEqual(response["log"]["status"], "ok")
        snapshot = json.loads((self.root / "data/uploads/web_case_001/config/config.json").read_text(encoding="utf-8"))
        self.assertEqual(snapshot["camera"]["image_width"], 1024)

    def test_run_reports_duplicate_task(self):
        payload = {
            "task_id": "web_case_002",
            "calibration_image": "data/samples/calibration/reference.png",
            "measurement_image": "data/samples/measurement/lens.png",
            "config_path": "config/default_config.json",
        }

        self.assertEqual(self.app.run(payload)["result"]["status"], "ok")
        duplicate = self.app.run(payload)

        self.assertEqual(duplicate["result"]["error"]["code"], "TASK_CONFLICT")

    def test_bundle_contains_input_package_and_referenced_files(self):
        payload = {
            "task_id": "web_case_bundle",
            "calibration_image": "data/samples/calibration/reference.png",
            "measurement_image": "data/samples/measurement/lens.png",
            "config_path": "config/default_config.json",
        }
        self.assertEqual(self.app.run(payload)["result"]["status"], "ok")

        bundle = self.app.integration_bundle("web_case_bundle")

        self.assertEqual(bundle["filename"], "m1_web_case_bundle_m2_integration_bundle.zip")
        with zipfile.ZipFile(BytesIO(bundle["data"])) as archive:
            names = set(archive.namelist())
            self.assertIn("input_package.json", names)
            self.assertIn("README_M1_M2_INTEGRATION.md", names)
            package = json.loads(archive.read("input_package.json").decode("utf-8"))
            for key in ("calibration_image", "measurement_image", "config_path"):
                self.assertIn(package["data"][key], names)

    def test_bundle_rejects_missing_referenced_file(self):
        payload = {
            "task_id": "web_case_missing_bundle_file",
            "calibration_image": "data/samples/calibration/reference.png",
            "measurement_image": "data/samples/measurement/lens.png",
            "config_path": "config/default_config.json",
        }
        self.assertEqual(self.app.run(payload)["result"]["status"], "ok")
        (self.root / "config/default_config.json").unlink()

        with self.assertRaisesRegex(WebError, "config/default_config.json"):
            self.app.integration_bundle("web_case_missing_bundle_file")


class WebServerTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        (self.root / "config").mkdir()
        (self.root / "data/samples/calibration").mkdir(parents=True)
        (self.root / "data/samples/measurement").mkdir(parents=True)
        (self.root / "data/samples/calibration/reference.png").write_bytes(b"calibration")
        (self.root / "data/samples/measurement/lens.png").write_bytes(b"measurement")
        source = PROJECT_ROOT / "config/default_config.json"
        (self.root / "config/default_config.json").write_text(source.read_text(encoding="utf-8"), encoding="utf-8")
        self.server = make_server(self.root, "127.0.0.1", 0)
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()
        self.base_url = f"http://127.0.0.1:{self.server.server_port}"

    def tearDown(self):
        self.server.shutdown()
        self.server.server_close()
        self.thread.join(timeout=2)
        self.temp.cleanup()

    def request_json(self, path, method="GET", body=None, headers=None):
        data = None if body is None else json.dumps(body).encode("utf-8")
        request = Request(self.base_url + path, data=data, method=method, headers=headers or {})
        with urlopen(request, timeout=3) as response:
            return response.status, json.loads(response.read().decode("utf-8"))

    def request_bytes(self, path):
        with urlopen(self.base_url + path, timeout=3) as response:
            return response.status, response.headers, response.read()

    def test_bootstrap_endpoint_returns_file_groups(self):
        status, body = self.request_json("/api/bootstrap")

        self.assertEqual(status, 200)
        self.assertEqual(set(body["files"]), {"calibration", "measurement", "config"})

    def test_upload_endpoint_accepts_raw_image(self):
        path = "/api/upload?kind=calibration&task_id=http_case&filename=" + quote("reference image.png")
        request = Request(
            self.base_url + path,
            data=b"image",
            method="POST",
            headers={"Content-Type": "application/octet-stream"},
        )

        with urlopen(request, timeout=3) as response:
            body = json.loads(response.read().decode("utf-8"))

        self.assertEqual(body["path"], "data/uploads/http_case/calibration/reference_image.png")

    def test_run_endpoint_rejects_malformed_json(self):
        request = Request(
            self.base_url + "/api/run",
            data=b"{broken",
            method="POST",
            headers={"Content-Type": "application/json"},
        )

        with self.assertRaises(HTTPError) as caught:
            urlopen(request, timeout=3)

        self.assertEqual(caught.exception.code, 400)
        body = json.loads(caught.exception.read().decode("utf-8"))
        caught.exception.close()
        self.assertEqual(body["error"]["code"], "INVALID_JSON")

    def test_unknown_api_returns_json_404(self):
        with self.assertRaises(HTTPError) as caught:
            urlopen(self.base_url + "/api/unknown", timeout=3)

        self.assertEqual(caught.exception.code, 404)
        body = json.loads(caught.exception.read().decode("utf-8"))
        caught.exception.close()
        self.assertEqual(body["error"]["code"], "NOT_FOUND")

    def test_bundle_endpoint_returns_downloadable_zip(self):
        payload = {
            "task_id": "http_bundle",
            "calibration_image": "data/samples/calibration/reference.png",
            "measurement_image": "data/samples/measurement/lens.png",
            "config_path": "config/default_config.json",
        }
        status, body = self.request_json(
            "/api/run",
            method="POST",
            body=payload,
            headers={"Content-Type": "application/json"},
        )
        self.assertEqual(status, 200)
        self.assertEqual(body["result"]["status"], "ok")

        status, headers, data = self.request_bytes("/api/task/http_bundle/bundle")

        self.assertEqual(status, 200)
        self.assertEqual(headers.get_content_type(), "application/zip")
        self.assertIn("m1_http_bundle_m2_integration_bundle.zip", headers.get("Content-Disposition"))
        with zipfile.ZipFile(BytesIO(data)) as archive:
            names = set(archive.namelist())
            self.assertIn("input_package.json", names)
            package = json.loads(archive.read("input_package.json").decode("utf-8"))
            for key in ("calibration_image", "measurement_image", "config_path"):
                self.assertIn(package["data"][key], names)


class StaticContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.static = PROJECT_ROOT / "modules/input_config/web/static"

    def test_page_contains_complete_six_step_workflow(self):
        html = (self.static / "index.html").read_text(encoding="utf-8")

        for step in range(1, 7):
            self.assertIn(f'data-step="{step}"', html)
        for text in ("任务信息", "标定图", "测量图", "配置参数", "确认并运行", "结果与日志"):
            self.assertIn(text, html)
        self.assertIn('id="run-m1"', html)
        self.assertIn('id="result-json"', html)
        self.assertIn('id="log-json"', html)
        self.assertIn('rel="icon"', html)

    def test_page_supports_existing_files_uploads_and_config_editing(self):
        html = (self.static / "index.html").read_text(encoding="utf-8")

        self.assertGreaterEqual(html.count('type="file"'), 3)
        self.assertIn('id="calibration-existing"', html)
        self.assertIn('id="measurement-existing"', html)
        self.assertIn('id="config-existing"', html)
        self.assertIn('id="config-fields"', html)

    def test_styles_cover_responsive_focus_and_reduced_motion(self):
        css = (self.static / "styles.css").read_text(encoding="utf-8")

        self.assertIn("@media (max-width:", css)
        self.assertIn("prefers-reduced-motion", css)
        self.assertIn(":focus-visible", css)

    def test_script_connects_bootstrap_upload_run_and_task_views(self):
        script = (self.static / "app.js").read_text(encoding="utf-8")

        for endpoint in ("/api/bootstrap", "/api/upload", "/api/run"):
            self.assertIn(endpoint, script)
        for function in ("goToStep", "uploadFile", "runM1", "renderResult"):
            self.assertIn(function, script)

    def test_step_four_exposes_chinese_parameter_labels_without_removing_keys(self):
        script = (self.static / "app.js").read_text(encoding="utf-8")

        for label in ("像素尺寸", "图像宽度", "Hartmann 点阵间距", "最低识别置信度", "允许绝对路径"):
            self.assertIn(label, script)
        for key in ("pixel_size_um", "image_width", "hartmann_spacing_mm", "min_confidence", "allow_absolute_path"):
            self.assertIn(key, script)

    def test_recent_task_can_open_result_without_new_task_validation(self):
        script = (self.static / "app.js").read_text(encoding="utf-8")

        self.assertIn("function goToStep(nextStep, force = false)", script)
        self.assertIn("goToStep(6, true)", script)

    def test_result_page_can_download_and_copy_integration_bundle(self):
        html = (self.static / "index.html").read_text(encoding="utf-8")
        script = (self.static / "app.js").read_text(encoding="utf-8")

        for element_id in ("download-bundle", "copy-bundle-note", "bundle-summary", "bundle-status"):
            self.assertIn(f'id="{element_id}"', html)
        self.assertIn("/bundle", script)
        self.assertIn("function downloadBundle", script)
        self.assertIn("function copyBundleNote", script)

    def test_page_can_prepare_stage_one_five_spot_bundle(self):
        html = (self.static / "index.html").read_text(encoding="utf-8")
        script = (self.static / "app.js").read_text(encoding="utf-8")

        self.assertIn('id="stage-one-five-spot"', html)
        self.assertIn("第一阶段五光斑联调包", html)
        self.assertIn("data/samples/calibration/calib_mock_001.jpg", script)
        self.assertIn("data/samples/measurement/meas_mock_001.jpg", script)
        self.assertIn("function prepareStageOneFiveSpot", script)
        self.assertIn("state.config.recognition.expected_spot_count = 5", script)


if __name__ == "__main__":
    unittest.main()
