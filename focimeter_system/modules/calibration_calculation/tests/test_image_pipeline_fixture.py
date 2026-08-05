import hashlib
import json
import unittest
from pathlib import Path

from modules.calibration_calculation.algorithm.calculator import calculate
from modules.calibration_calculation.validator.contract_validator import validate_result


ROOT = Path(__file__).resolve().parents[3]
MODULE_ROOT = ROOT / "modules" / "calibration_calculation"
FIXTURE_ROOT = MODULE_ROOT / "tests" / "fixtures" / "m2_experimental_known_prescription"


def read_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def normalized_sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes().replace(b"\r\n", b"\n")).hexdigest()


class ImagePipelineFixtureTests(unittest.TestCase):
    def setUp(self) -> None:
        self.manifest = read_json(FIXTURE_ROOT / "manifest.json")
        self.calibration = read_json(FIXTURE_ROOT / "spots_calib_multispot.json")
        self.measurement = read_json(FIXTURE_ROOT / "spots_meas_multispot.json")
        self.config = read_json(ROOT / "config" / "default_config.json")
        self.model = read_json(
            MODULE_ROOT / "examples" / "calibration" / "calibration_model.image_pipeline_simulation.json"
        )

    def test_fixed_m2_outputs_match_manifest(self) -> None:
        for filename, metadata in self.manifest["files"].items():
            path = FIXTURE_ROOT / filename
            self.assertEqual(metadata["sha256_lf"], normalized_sha256(path))
            document = read_json(path)
            self.assertEqual("ok", document["status"])
            self.assertEqual(metadata["detected_count"], document["quality"]["detected_count"])

    def test_rendered_image_outputs_recover_known_prescription(self) -> None:
        result = calculate(
            self.calibration,
            self.measurement,
            self.config,
            self.model,
            allow_simulation_model=True,
        )

        self.assertEqual("ok", result["status"], result)
        self.assertEqual("cylindrical", result["lens_type"])
        self.assertAlmostEqual(-2.0, result["result"]["S"], delta=0.01)
        self.assertAlmostEqual(-1.0, result["result"]["C"], delta=0.01)
        self.assertAlmostEqual(45.0, result["result"]["A"], delta=0.1)
        self.assertEqual(94, result["quality"]["matched_spot_count"])
        self.assertLess(result["quality"]["fit_rmse"], 0.05)
        self.assertTrue(validate_result(result).valid)


if __name__ == "__main__":
    unittest.main()
