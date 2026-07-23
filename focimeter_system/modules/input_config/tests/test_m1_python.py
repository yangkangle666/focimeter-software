import json
import tempfile
import unittest
from pathlib import Path

from modules.input_config import run_m1


PROJECT_ROOT = Path(__file__).resolve().parents[3]


class M1ContractTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        (self.root / "data/samples/calibration").mkdir(parents=True)
        (self.root / "data/samples/measurement").mkdir(parents=True)
        (self.root / "config").mkdir()
        (self.root / "data/samples/calibration/calib_mock_001.jpg").write_bytes(b"calibration")
        (self.root / "data/samples/measurement/meas_mock_001.jpg").write_bytes(b"measurement")
        config = json.loads((PROJECT_ROOT / "config/default_config.json").read_text(encoding="utf-8"))
        (self.root / "config/default_config.json").write_text(json.dumps(config), encoding="utf-8")

    def tearDown(self):
        self.temp.cleanup()

    def request(self, task_id="sample_001"):
        return {
            "schema_version": "1.0", "task_id": task_id, "module": "m1_input_config", "status": "ok",
            "request": {"calibration_image": "data/samples/calibration/calib_mock_001.jpg", "measurement_image": "data/samples/measurement/meas_mock_001.jpg", "config_path": "config/default_config.json", "run_mode": "local_image", "operator": "mock_user", "notes": "测试"},
            "error": None,
        }

    def test_output_matches_v1_envelope(self):
        result = run_m1(self.request(), self.root)
        self.assertEqual(result["status"], "ok")
        self.assertEqual(set(result), {"schema_version", "task_id", "module", "status", "data", "quality", "error"})
        self.assertEqual(result["module"], "m1_input_config")
        self.assertTrue(result["quality"]["paths_checked"])
        self.assertTrue(result["quality"]["config_checked"])
        self.assertEqual(result["data"]["config_path"], "config/default_config.json")
        self.assertIn(
            "SOFTWARE_INTEGRATION_ONLY: 仅表示路径和 JSON 契约可用于软件联调，不代表真实计量验证完成。",
            result["quality"]["warnings"],
        )

    def test_real_input_package_paths_resolve(self):
        package_path = PROJECT_ROOT / "data/mock/m1_input_config/input_package_real_data.json"
        self.assertTrue(package_path.is_file(), f"missing integration package: {package_path}")
        package = json.loads(package_path.read_text(encoding="utf-8"))
        self.assertTrue(package["quality"]["paths_checked"])
        self.assertTrue(package["quality"]["is_usable"])
        self.assertTrue(any(warning.startswith("SOFTWARE_INTEGRATION_ONLY:") for warning in package["quality"]["warnings"]))

        package_paths = (package["data"][key] for key in ("calibration_image", "measurement_image", "config_path"))
        missing = [path for path in package_paths if not (PROJECT_ROOT / path).is_file()]
        self.assertEqual(missing, [], f"missing integration fixture files: {missing}")

    def test_missing_image_matches_error_contract(self):
        request = self.request("sample_missing_image")
        request["request"]["calibration_image"] = "data/samples/calibration/not_found.jpg"
        result = run_m1(request, self.root)
        self.assertEqual(result["status"], "error")
        self.assertEqual(result["error"]["code"], "IMAGE_NOT_FOUND")
        self.assertEqual(result["error"]["details"]["missing_field"], "calibration_image")

    def test_invalid_config_returns_config_invalid(self):
        config = json.loads((self.root / "config/default_config.json").read_text(encoding="utf-8"))
        config["image_processing"]["median_kernel"] = 4
        (self.root / "config/default_config.json").write_text(json.dumps(config), encoding="utf-8")
        result = run_m1(self.request("sample_invalid_config"), self.root)
        self.assertEqual(result["error"]["code"], "CONFIG_INVALID")

    def test_absolute_path_is_rejected(self):
        request = self.request("sample_absolute_path")
        request["request"]["calibration_image"] = str(self.root / "data/samples/calibration/calib_mock_001.jpg")
        result = run_m1(request, self.root)
        self.assertEqual(result["error"]["code"], "IMAGE_NOT_FOUND")


if __name__ == "__main__":
    unittest.main()
