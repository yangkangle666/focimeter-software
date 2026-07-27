import json
import hashlib
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from modules.input_config import run_m1
from modules.input_config.calibration import validate_calibration
from modules.input_config.validation import validate_config


PROJECT_ROOT = Path(__file__).resolve().parents[3]


class M1ContractTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        (self.root / "data/samples/calibration").mkdir(parents=True)
        (self.root / "data/samples/measurement").mkdir(parents=True)
        (self.root / "data/calibration").mkdir(parents=True)
        (self.root / "config").mkdir()
        (self.root / "data/samples/calibration/calib_mock_001.jpg").write_bytes(b"calibration")
        (self.root / "data/samples/measurement/meas_mock_001.jpg").write_bytes(b"measurement")
        config = self.default_config()
        (self.root / "config/default_config.json").write_text(json.dumps(config), encoding="utf-8")
        (self.root / "data/calibration/simulation_calibration.json").write_text(
            json.dumps(self.simulation_calibration()),
            encoding="utf-8",
        )

    def tearDown(self):
        self.temp.cleanup()

    def request(self, task_id="sample_001"):
        return {
            "schema_version": "1.0", "task_id": task_id, "module": "m1_input_config", "status": "ok",
            "request": {"calibration_image": "data/samples/calibration/calib_mock_001.jpg", "measurement_image": "data/samples/measurement/meas_mock_001.jpg", "config_path": "config/default_config.json", "run_mode": "local_image", "operator": "mock_user", "notes": "测试"},
            "error": None,
        }

    def default_config(self):
        return json.loads((PROJECT_ROOT / "config/default_config.json").read_text(encoding="utf-8"))

    def simulation_calibration(self):
        return json.loads(
            (PROJECT_ROOT / "data/calibration/simulation_calibration.json").read_text(encoding="utf-8")
        )

    def write_config(self, config, name="default_config.json"):
        path = self.root / "config" / name
        path.write_text(json.dumps(config), encoding="utf-8")
        return path

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

    def test_multispot_package_reports_provenance_and_calibration_warnings(self):
        result = run_m1(self.request("multispot_package"), self.root)
        self.assertEqual(result["status"], "ok")
        self.assertIn("DATA_SOURCE: synthetic", result["quality"]["warnings"])
        self.assertIn("VALIDATION_STATUS: simulation_only", result["quality"]["warnings"])
        self.assertIn(
            "HARDWARE_PARAMETERS_UNCONFIRMED: calibration.parameters",
            result["quality"]["warnings"],
        )
        self.assertIn(
            "CALIBRATION_PARAMETER_PENDING: parameters.hartmann_spacing_mm",
            result["quality"]["warnings"],
        )

    def test_missing_referenced_calibration_returns_config_not_found(self):
        config = self.default_config()
        config["calibration_reference"]["calibration_file"] = "data/calibration/missing.json"
        self.write_config(config)
        result = run_m1(self.request("missing_calibration"), self.root)
        self.assertEqual(result["error"]["code"], "CONFIG_NOT_FOUND")
        self.assertEqual(result["error"]["details"]["missing_field"], "calibration_file")

    def test_legacy_five_spot_package_has_compatibility_warning(self):
        config = json.loads(
            (PROJECT_ROOT / "config/legacy_five_spot_config.json").read_text(encoding="utf-8")
        )
        self.write_config(config, "legacy_five_spot_config.json")
        request = self.request("legacy_five_spot")
        request["request"]["config_path"] = "config/legacy_five_spot_config.json"
        result = run_m1(request, self.root)
        self.assertEqual(result["status"], "ok")
        self.assertIn(
            "LEGACY_FIVE_SPOT_COMPATIBILITY: 仅用于旧接口兼容测试，不是 LM700 / Hartmann 正式算法目标。",
            result["quality"]["warnings"],
        )

    def test_log_hashes_referenced_calibration_file(self):
        task_id = "calibration_hash"
        result = run_m1(self.request(task_id), self.root)
        self.assertEqual(result["status"], "ok")
        log = json.loads(
            (self.root / f"outputs/logs/{task_id}_input_config.json").read_text(encoding="utf-8")
        )
        self.assertIn("calibration_file", log["sha256"])
        self.assertIn("data/calibration/simulation_calibration.json", log["input_files"])

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
        config = self.default_config()
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

    def test_simulation_calibration_declares_exact_simulated_parameters(self):
        calibration = self.simulation_calibration()
        self.assertEqual(calibration, {
            "schema_version": "1.0",
            "calibration_version": "simulation-v1",
            "parameter_status": "simulated",
            "validation_status": "simulation_only",
            "hardware_parameters_confirmed": False,
            "parameters": {
                "pixel_pitch_mm": 0.0048,
                "effective_focal_length_mm": 12.0,
                "distance_m": 0.03,
                "hartmann_spacing_mm": None,
                "optical_magnification": None,
                "power_sign": -1.0,
                "wavelength_nm": None,
            },
        })

    def test_legacy_five_spot_config_declares_fixed_five_spot_profile(self):
        config = json.loads(
            (PROJECT_ROOT / "config/legacy_five_spot_config.json").read_text(encoding="utf-8")
        )
        self.assertEqual(config["config_name"], "legacy_five_spot_config")
        self.assertEqual(config["recognition"], {
            "spot_count_mode": "fixed",
            "expected_spot_count": 5,
            "min_confidence": 0.7,
        })
        self.assertEqual(config["data_profile"], {
            "data_source": "mock",
            "validation_status": "simulation_only",
            "hardware_parameters_confirmed": False,
        })

    def test_auto_spot_count_rejects_fixed_expected_count(self):
        config = self.default_config()
        config["recognition"]["expected_spot_count"] = 5
        _, error = validate_config(config)
        self.assertEqual(error.code, "CONFIG_INVALID")

    def test_fixed_spot_count_requires_positive_expected_count(self):
        config = self.default_config()
        config["recognition"] = {
            "spot_count_mode": "fixed",
            "expected_spot_count": None,
            "min_confidence": 0.7,
        }
        _, error = validate_config(config)
        self.assertEqual(error.code, "CONFIG_INVALID")

    def test_metrology_status_requires_real_confirmed_hardware(self):
        config = self.default_config()
        config["data_profile"]["validation_status"] = "metrology_validated"
        _, error = validate_config(config)
        self.assertEqual(error.code, "CONFIG_INVALID")

    def test_legacy_recognition_shape_remains_accepted(self):
        config = self.default_config()
        config.pop("data_profile")
        config.pop("calibration_reference")
        config["recognition"] = {"expected_spot_count": 5, "min_confidence": 0.7}
        warnings, error = validate_config(config)
        self.assertIsNone(error)
        self.assertIn(
            "CONFIG_PROFILE_LEGACY: provenance and calibration metadata are absent",
            warnings,
        )

    def test_calibration_version_must_match_config_reference(self):
        config = self.default_config()
        calibration = self.simulation_calibration()
        calibration["calibration_version"] = "different-version"
        _, error = validate_calibration(calibration, config)
        self.assertEqual(error.code, "CONFIG_INVALID")

    def test_simulation_calibration_matches_default_config(self):
        warnings, error = validate_calibration(
            self.simulation_calibration(),
            self.default_config(),
        )
        self.assertIsNone(error)
        self.assertIn(
            "CALIBRATION_PARAMETER_PENDING: parameters.hartmann_spacing_mm",
            warnings,
        )

    def test_multispot_fixture_pngs_have_expected_dimensions_and_differ(self):
        paths = [
            PROJECT_ROOT / "data/synthetic/generated_images/hartmann_reference.png",
            PROJECT_ROOT / "data/synthetic/generated_images/hartmann_measurement.png",
        ]
        dimensions = []
        for path in paths:
            png = path.read_bytes()
            width, height, bit_depth, color_type, _, _, _ = struct.unpack(">IIBBBBB", png[16:29])
            dimensions.append((width, height, bit_depth, color_type))
        self.assertEqual(dimensions, [(640, 480, 8, 0), (640, 480, 8, 0)])
        self.assertNotEqual(paths[0].read_bytes(), paths[1].read_bytes())

    def test_hartmann_fixture_generation_is_byte_deterministic(self):
        paths = [
            PROJECT_ROOT / "data/synthetic/generated_images/hartmann_reference.png",
            PROJECT_ROOT / "data/synthetic/generated_images/hartmann_measurement.png",
        ]
        before = [hashlib.sha256(path.read_bytes()).digest() for path in paths]
        subprocess.run(
            [sys.executable, str(PROJECT_ROOT / "tools/generate_hartmann_fixtures.py")],
            cwd=PROJECT_ROOT,
            check=True,
            capture_output=True,
            text=True,
        )
        after = [hashlib.sha256(path.read_bytes()).digest() for path in paths]
        self.assertEqual(after, before)

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

    def test_mock_packages_declare_data_and_validation_state(self):
        expected = {
            "input_package_ok.json": ("DATA_SOURCE: mock", "VALIDATION_STATUS: simulation_only"),
            "input_package_real_data.json": ("DATA_SOURCE: real", "VALIDATION_STATUS: software_verified"),
        }
        for name, declarations in expected.items():
            package = json.loads(
                (PROJECT_ROOT / "data/mock/m1_input_config" / name).read_text(encoding="utf-8")
            )
            warnings = package["quality"]["warnings"]
            for declaration in declarations:
                self.assertIn(declaration, warnings)
            self.assertFalse(any("metrology_validated" in warning for warning in warnings))

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
