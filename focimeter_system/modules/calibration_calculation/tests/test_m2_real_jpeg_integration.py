import copy
import json
import unittest
from pathlib import Path

from modules.calibration_calculation.algorithm.calculator import calculate
from modules.calibration_calculation.algorithm.types import CalibrationModel


PROJECT_ROOT = Path(__file__).resolve().parents[3]
MODULE_ROOT = Path(__file__).resolve().parents[1]
SAMPLE_ROOT = (
    PROJECT_ROOT
    / "modules"
    / "image_recognition"
    / "samples"
    / "real_jpeg_software_verified"
)


class M2RealJpegIntegrationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.config = json.loads(
            (PROJECT_ROOT / "config" / "default_config.json").read_text(encoding="utf-8")
        )
        cls.model = CalibrationModel.from_dict(json.loads(
            (
                MODULE_ROOT
                / "examples"
                / "calibration"
                / "calibration_model.simulation.json"
            ).read_text(encoding="utf-8")
        ))

    def _load_pair(self, pair_name: str) -> tuple[dict, dict]:
        pair_root = SAMPLE_ROOT / pair_name
        calibration = json.loads(
            (pair_root / "spots_calib_multispot.json").read_text(encoding="utf-8")
        )
        measurement = json.loads(
            (pair_root / "spots_meas_multispot.json").read_text(encoding="utf-8")
        )
        return calibration, measurement

    def _assert_identity_error(self, result: dict) -> None:
        self.assertEqual("error", result["status"], result)
        self.assertEqual("COORDINATE_SYSTEM_INVALID", result["error"]["code"], result)
        self.assertNotIn("result", result)
        self.assertNotIn("lens_type", result)
        serialized = json.dumps(result, sort_keys=True)
        for prescription_key in ('"S"', '"C"', '"A"'):
            self.assertNotIn(prescription_key, serialized)

    def test_original_real_pairs_reject_unsafe_retained_spot_flags(self) -> None:
        for pair_name in ("pair_1", "pair_2"):
            with self.subTest(pair=pair_name):
                calibration, measurement = self._load_pair(pair_name)
                result = calculate(
                    calibration,
                    measurement,
                    self.config,
                    self.model,
                    allow_simulation_model=True,
                )

                self._assert_identity_error(result)
                self.assertIn("unsafe quality flags", result["error"]["message"])

    def test_pair_2_without_quality_flags_still_rejects_incomplete_identity_assignment(self) -> None:
        calibration, measurement = self._load_pair("pair_2")
        calibration = copy.deepcopy(calibration)
        measurement = copy.deepcopy(measurement)
        for document in (calibration, measurement):
            document["quality"]["warnings"] = []
            for spot in document["spots"]:
                spot["quality_flags"] = []

        result = calculate(
            calibration,
            measurement,
            self.config,
            self.model,
            allow_simulation_model=True,
        )

        self._assert_identity_error(result)
        self.assertIn("23 of 27", result["error"]["message"])
        self.assertIn("every measurement detection", result["error"]["message"])


if __name__ == "__main__":
    unittest.main()
