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

    def test_default_config_declares_simulated_camera_green_light_and_coordinate_system(self):
        config = json.loads((PROJECT_ROOT / "config/default_config.json").read_text(encoding="utf-8"))

        self.assertEqual(config["camera"]["image_width"], 1280)
        self.assertEqual(config["camera"]["image_height"], 1024)
        self.assertEqual(config["camera"]["pixel_size_um"], 4.8)
        self.assertEqual(config["camera_simulation"]["bit_depth"], 8)
        self.assertEqual(config["camera_simulation"]["color_mode"], "mono")
        self.assertEqual(config["camera_simulation"]["exposure_min_ms"], 0.01)
        self.assertEqual(config["camera_simulation"]["exposure_max_ms"], 100.0)
        self.assertEqual(config["illumination"]["source_color"], "green_led")
        self.assertEqual(config["coordinate_system"]["y_positive"], "down")
        self.assertFalse(config["coordinate_system"]["y_flip"])
        self.assertEqual(config["measurement_targets"]["sphere_min_d"], -25.0)
        self.assertEqual(config["measurement_targets"]["sphere_max_d"], 25.0)

    def test_default_config_uses_multispot_simulation_profile(self):
        config = json.loads((PROJECT_ROOT / "config/default_config.json").read_text(encoding="utf-8"))
        self.assertEqual(config["recognition"], {
            "spot_count_mode": "auto",
            "expected_spot_count": None,
            "min_confidence": 0.7,
        })
        self.assertEqual(config["data_profile"], {
            "data_source": "synthetic",
            "validation_status": "simulation_only",
            "hardware_parameters_confirmed": False,
        })
        self.assertEqual(
            config["calibration_reference"]["calibration_file"],
            "data/calibration/simulation_calibration.json",
        )

    def test_multispot_fixture_assets_are_real_png_files(self):
        paths = [
            PROJECT_ROOT / "data/synthetic/generated_images/hartmann_reference.png",
            PROJECT_ROOT / "data/synthetic/generated_images/hartmann_measurement.png",
        ]
        for path in paths:
            self.assertTrue(path.is_file())
            self.assertEqual(path.read_bytes()[:8], b"\x89PNG\r\n\x1a\n")
            self.assertGreater(path.stat().st_size, 1024)

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

    def test_fl800_green_device_profile_is_accepted(self):
        config = json.loads((self.root / "config/default_config.json").read_text(encoding="utf-8"))
        config["config_name"] = "fl800_green_config"
        config["camera"].update({"image_width": 1290, "image_height": 826})
        config["illumination"] = {"source_color": "green", "wavelength_nm": None}
        config["hartmann_calibration"] = {
            "spacing_source": "camera_pixel_spacing",
            "spot_spacing_px": None,
            "spacing_formula": "spot_spacing_px * camera.pixel_size_um / 1000",
        }
        config["measurement_targets"] = {
            "sphere_min_d": -25.0, "sphere_max_d": 25.0, "sphere_steps_d": [0.01, 0.06, 0.12, 0.25],
            "cylinder_min_d": -10.0, "cylinder_max_d": 10.0, "cylinder_steps_d": [0.01, 0.06, 0.12, 0.25],
            "prism_min_delta": 0.0, "prism_max_delta": 15.0, "prism_step_delta": 0.01,
            "axis_min_degree": 0, "axis_max_degree": 180, "axis_step_degree": 1,
            "addition_min_d": 0.0, "addition_max_d": 10.0, "addition_steps_d": [0.01, 0.06, 0.12, 0.25],
            "uv_min_percent": 1, "uv_max_percent": 100, "uv_steps_percent": [1, 15],
        }
        config_path = self.root / "config/fl800_green_config.json"
        config_path.write_text(json.dumps(config), encoding="utf-8")
        request = self.request("sample_fl800_green")
        request["request"]["config_path"] = "config/fl800_green_config.json"

        result = run_m1(request, self.root)

        self.assertEqual(result["status"], "ok")
        self.assertIn("CONFIG_PARAMETER_PENDING: illumination.wavelength_nm", result["quality"]["warnings"])
        self.assertIn("CONFIG_PARAMETER_PENDING: hartmann_calibration.spot_spacing_px", result["quality"]["warnings"])

    def test_fl800_measurement_range_rejects_invalid_axis(self):
        config = json.loads((self.root / "config/default_config.json").read_text(encoding="utf-8"))
        config["config_name"] = "fl800_green_config"
        config["illumination"] = {"source_color": "green", "wavelength_nm": None}
        config["hartmann_calibration"] = {
            "spacing_source": "camera_pixel_spacing", "spot_spacing_px": None,
            "spacing_formula": "spot_spacing_px * camera.pixel_size_um / 1000",
        }
        config["measurement_targets"] = {
            "sphere_min_d": -25.0, "sphere_max_d": 25.0, "sphere_steps_d": [0.01],
            "cylinder_min_d": -10.0, "cylinder_max_d": 10.0, "cylinder_steps_d": [0.01],
            "prism_min_delta": 0.0, "prism_max_delta": 15.0, "prism_step_delta": 0.01,
            "axis_min_degree": 0, "axis_max_degree": 181, "axis_step_degree": 1,
            "addition_min_d": 0.0, "addition_max_d": 10.0, "addition_steps_d": [0.01],
            "uv_min_percent": 1, "uv_max_percent": 100, "uv_steps_percent": [1],
        }
        (self.root / "config/fl800_green_config.json").write_text(json.dumps(config), encoding="utf-8")
        request = self.request("sample_fl800_invalid_axis")
        request["request"]["config_path"] = "config/fl800_green_config.json"

        result = run_m1(request, self.root)

        self.assertEqual(result["error"]["code"], "CONFIG_INVALID")

    def test_absolute_path_is_rejected(self):
        request = self.request("sample_absolute_path")
        request["request"]["calibration_image"] = str(self.root / "data/samples/calibration/calib_mock_001.jpg")
        result = run_m1(request, self.root)
        self.assertEqual(result["error"]["code"], "IMAGE_NOT_FOUND")


if __name__ == "__main__":
    unittest.main()
