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
from modules.calibration_calculation.tests.test_multispot_matching import lattice_pair
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

    def experimental_document(self, image_type: str, observations) -> dict:
        return {
            "schema_version": "m2.multispot.experimental.1",
            "task_id": "experimental_calculation",
            "module": "m2_image_recognition",
            "status": "ok",
            "experimental": True,
            "contract_status": "proposed",
            "validation_status": "software_verified",
            "validation_scope": "simulation_only",
            "metrology_validated": False,
            "image_type": image_type,
            "coordinate_type": "image_pixel",
            "spots": [
                {"detection_id": item.detection_id, "x": item.x, "y": item.y, "confidence": item.confidence}
                for item in observations
            ],
            "quality": {"detected_count": len(observations), "is_usable": True, "warnings": ["MOCK_DATA_ONLY"]},
            "matching": {"status": "not_performed", "id_scope": "image_local", "physical_identity_guaranteed": False},
            "error": None,
        }

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
        self.assertIn("software_verified", result["quality"]["warnings"])
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

    def test_quality_warnings_may_be_omitted(self) -> None:
        calibration = copy.deepcopy(self.calibration)
        calibration["quality"].pop("warnings")
        measurement = self.measurement_for(Prescription(1.0, 0.0, None))
        measurement["quality"].pop("warnings")

        result = calculate(
            calibration, measurement, self.config, self.model, allow_simulation_model=True
        )

        self.assertEqual("ok", result["status"], result)

    def test_malformed_model_range_is_a_configuration_error(self) -> None:
        measurement = self.measurement_for(Prescription(1.0, 0.0, None))
        model = self.model.to_dict()
        model["quality_limits"]["validated_sphere_range_D"] = [-5.0]

        result = calculate(
            self.calibration, measurement, self.config, model, allow_simulation_model=True
        )

        self.assertEqual("CONFIG_INVALID", result["error"]["code"], result)

    def test_experimental_full_coverage_is_matched_before_calculation(self) -> None:
        pair = lattice_pair(
            measurement_count=43,
            randomize=True,
            transform=[[0.98, 0.0], [0.0, 0.98]],
        )
        calibration = self.experimental_document("calibration", pair.calibration)
        measurement = self.experimental_document("measurement", pair.measurement)
        result = calculate(calibration, measurement, self.config, self.model, allow_simulation_model=True)
        self.assertEqual("ok", result["status"], result)
        self.assertEqual(43, result["quality"]["matched_spot_count"])
        self.assertEqual("m2.multispot.experimental.1", result["matching"]["input_schema_version"])
        self.assertEqual(43, result["matching"]["calibration_detection_count"])
        self.assertTrue(validate_result(result).valid)

    def test_experimental_partial_overlap_returns_coordinate_error(self) -> None:
        pair = lattice_pair(measurement_count=27, randomize=True)
        result = calculate(
            self.experimental_document("calibration", pair.calibration),
            self.experimental_document("measurement", pair.measurement),
            self.config,
            self.model,
            allow_simulation_model=True,
        )
        self.assertEqual("error", result["status"])
        self.assertEqual("COORDINATE_SYSTEM_INVALID", result["error"]["code"])
        self.assertIn("Every detected spot", result["error"]["message"])

    def test_mixed_input_contracts_are_rejected(self) -> None:
        pair = lattice_pair(measurement_count=27)
        measurement = self.experimental_document("measurement", pair.measurement)
        result = calculate(self.calibration, measurement, self.config, self.model, allow_simulation_model=True)
        self.assertEqual("CONFIG_INVALID", result["error"]["code"])

    def test_ambiguous_experimental_input_returns_coordinate_error(self) -> None:
        pair = lattice_pair(
            measurement_count=37,
            symmetric=True,
            transform=[[1.0, 0.0], [0.0, 1.0]],
            translation=(0.0, 0.0),
        )
        result = calculate(
            self.experimental_document("calibration", pair.calibration),
            self.experimental_document("measurement", pair.measurement),
            self.config,
            self.model,
            allow_simulation_model=True,
        )
        self.assertEqual("COORDINATE_SYSTEM_INVALID", result["error"]["code"])


if __name__ == "__main__":
    unittest.main()
