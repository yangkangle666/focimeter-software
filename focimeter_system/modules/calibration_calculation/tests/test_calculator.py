import copy
import json
import unittest
from pathlib import Path

import numpy as np

from modules.calibration_calculation.algorithm.calculator import calculate
from modules.calibration_calculation.algorithm.power_vector import (
    power_vector_to_matrix,
    prescription_to_power_vector,
)
from modules.calibration_calculation.algorithm.types import CalibrationModel, Prescription
from modules.calibration_calculation.tests.test_geometry import transformed_measurement
from modules.calibration_calculation.validator.contract_validator import validate_result


ROOT = Path(__file__).resolve().parents[3]
MODULE_ROOT = ROOT / "modules" / "calibration_calculation"


class CalculatorTests(unittest.TestCase):
    def setUp(self) -> None:
        self.calibration = json.loads(
            (ROOT / "data/mock/m2_image_recognition/spots_calib_ok.json").read_text(encoding="utf-8")
        )
        self.config = json.loads((ROOT / "config/default_config.json").read_text(encoding="utf-8"))
        model_data = json.loads(
            (MODULE_ROOT / "examples/calibration/calibration_model.simulation.json").read_text(encoding="utf-8")
        )
        self.model = CalibrationModel.from_dict(model_data)

    def measurement_for(self, prescription: Prescription, translation=(0.0, 0.0)) -> dict:
        matrix = power_vector_to_matrix(prescription_to_power_vector(prescription))
        transform = np.eye(2) - self.config["optical"]["distance_m"] * matrix
        return transformed_measurement(self.calibration, transform, translation)

    def test_spherical_result_preserves_public_contract(self) -> None:
        measurement = self.measurement_for(Prescription(-2.5, 0.0, None), translation=(7.0, -3.0))
        result = calculate(
            self.calibration, measurement, self.config, self.model, allow_simulation_model=True
        )
        self.assertEqual("ok", result["status"], result)
        self.assertEqual("spherical", result["lens_type"])
        self.assertAlmostEqual(-2.5, result["result"]["S"], places=9)
        self.assertEqual(0.0, result["result"]["C"])
        self.assertIsNone(result["result"]["A"])
        self.assertIn("MOCK_DATA_ONLY", result["quality"]["warnings"])
        self.assertTrue(validate_result(result).valid)

    def test_cylindrical_axis_is_recovered(self) -> None:
        measurement = self.measurement_for(Prescription(-2.0, -1.5, 45.0))
        result = calculate(
            self.calibration, measurement, self.config, self.model, allow_simulation_model=True
        )
        self.assertEqual("ok", result["status"], result)
        self.assertAlmostEqual(-2.0, result["result"]["S"], places=9)
        self.assertAlmostEqual(-1.5, result["result"]["C"], places=9)
        self.assertAlmostEqual(45.0, result["result"]["A"], places=9)

    def test_production_rejects_simulation_model(self) -> None:
        measurement = self.measurement_for(Prescription(1.0, 0.0, None))
        result = calculate(self.calibration, measurement, self.config, self.model)
        self.assertEqual("error", result["status"])
        self.assertEqual("CONFIG_INVALID", result["error"]["code"])

    def test_out_of_range_result_is_rejected(self) -> None:
        measurement = self.measurement_for(Prescription(6.0, 0.0, None))
        result = calculate(
            self.calibration, measurement, self.config, self.model, allow_simulation_model=True
        )
        self.assertEqual("CALCULATION_FAILED", result["error"]["code"])
        self.assertEqual("RESULT_OUTSIDE_VALIDATED_RANGE", result["error"]["details"]["reason"])

    def test_model_distance_must_match_config(self) -> None:
        measurement = self.measurement_for(Prescription(1.0, 0.0, None))
        config = copy.deepcopy(self.config)
        config["optical"]["distance_m"] = 0.04
        result = calculate(self.calibration, measurement, config, self.model, allow_simulation_model=True)
        self.assertEqual("CONFIG_INVALID", result["error"]["code"])


if __name__ == "__main__":
    unittest.main()
