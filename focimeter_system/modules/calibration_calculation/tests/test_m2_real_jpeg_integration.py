import copy
import json
import math
import unittest
from pathlib import Path

from jsonschema import Draft202012Validator
import numpy as np

from modules.calibration_calculation.algorithm.calculator import calculate
from modules.calibration_calculation.algorithm.multispot_matching import (
    _align_measurement_basis,
    _candidate_bases,
    _estimate_pitch,
)
from modules.calibration_calculation.algorithm.types import CalibrationModel, MatchingLimits
from modules.calibration_calculation.validator.contract_validator import validate_result


ROOT = Path(__file__).resolve().parents[3]
M2_SAMPLE_ROOT = ROOT / "modules" / "image_recognition" / "samples" / "real_jpeg_software_verified"
M3_MODULE_ROOT = ROOT / "modules" / "calibration_calculation"


class M2RealJpegIntegrationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.schema = json.loads(
            (M3_MODULE_ROOT / "schemas" / "m2_multispot_experimental.schema.json").read_text(encoding="utf-8")
        )
        cls.config = json.loads((ROOT / "config" / "default_config.json").read_text(encoding="utf-8"))
        cls.model = CalibrationModel.from_dict(json.loads(
            (M3_MODULE_ROOT / "examples" / "calibration" / "calibration_model.simulation.json").read_text(
                encoding="utf-8"
            )
        ))
        cls.pairs = {}
        for pair_name in ("pair_1", "pair_2"):
            pair_root = M2_SAMPLE_ROOT / pair_name
            cls.pairs[pair_name] = (
                json.loads((pair_root / "spots_calib_multispot.json").read_text(encoding="utf-8")),
                json.loads((pair_root / "spots_meas_multispot.json").read_text(encoding="utf-8")),
            )

    def test_all_four_documents_conform_to_the_m3_experimental_schema(self) -> None:
        validator = Draft202012Validator(self.schema)
        documents = [document for pair in self.pairs.values() for document in pair]
        self.assertEqual(4, len(documents))
        for document in documents:
            with self.subTest(task_id=document["task_id"], image_type=document["image_type"]):
                errors = sorted(validator.iter_errors(document), key=lambda error: list(error.absolute_path))
                self.assertEqual([], errors, [error.message for error in errors])
                self.assertEqual("software_only", document["validation_scope"])
                self.assertFalse(document["metrology_validated"])
                self.assertTrue(document["quality"]["is_usable"])
                self.assertEqual(27, len(document["spots"]))
                self.assertFalse(document["matching"]["physical_identity_guaranteed"])
                self.assertTrue(all("spot_id" not in spot for spot in document["spots"]))

    def test_identity_risk_markers_are_preserved_at_known_locations(self) -> None:
        pair_1_calibration, pair_1_measurement = self.pairs["pair_1"]
        pair_2_calibration, pair_2_measurement = self.pairs["pair_2"]

        def flags_by_detection(document: dict) -> dict[int, set[str]]:
            return {
                int(spot["detection_id"]): set(spot.get("quality_flags", []))
                for spot in document["spots"]
            }

        self.assertIn("AREA_ABOVE_MEDIAN", flags_by_detection(pair_1_calibration)[14])
        self.assertIn("AREA_ABOVE_MEDIAN", flags_by_detection(pair_2_calibration)[14])
        pair_1_measurement_flags = flags_by_detection(pair_1_measurement)
        self.assertIn("SUBPITCH_FRAGMENT_NEIGHBOR_REJECTED", pair_1_measurement_flags[13])
        self.assertIn("SUBPITCH_FRAGMENT_NEIGHBOR_REJECTED", pair_1_measurement_flags[18])
        self.assertIn(
            "SUBPITCH_FRAGMENT_REJECTED_UNVERIFIED",
            pair_1_measurement["quality"]["warnings"],
        )
        self.assertTrue(any(
            "AREA_ABOVE_MEDIAN" in flags
            for flags in flags_by_detection(pair_2_measurement).values()
        ))

    def test_real_absolute_installation_angle_is_not_treated_as_relative_rotation(self) -> None:
        limit = MatchingLimits.simulation_defaults().max_rotation_degree
        for pair_name, (calibration, measurement) in self.pairs.items():
            with self.subTest(pair_name=pair_name):
                calibration_points = np.asarray(
                    [(spot["x"], spot["y"]) for spot in calibration["spots"]],
                    dtype=float,
                )
                measurement_points = np.asarray(
                    [(spot["x"], spot["y"]) for spot in measurement["spots"]],
                    dtype=float,
                )
                calibration_basis = _candidate_bases(
                    calibration_points,
                    _estimate_pitch(calibration_points),
                )[0]
                measurement_basis = _candidate_bases(
                    measurement_points,
                    _estimate_pitch(measurement_points),
                )[0]
                absolute_angle = abs(math.degrees(math.atan2(
                    float(calibration_basis[1, 0]),
                    float(calibration_basis[0, 0]),
                )))
                self.assertGreater(absolute_angle, 40.0)
                aligned = _align_measurement_basis(calibration_basis, measurement_basis, limit)
                relative = aligned @ np.linalg.inv(calibration_basis)
                u, _, vt = np.linalg.svd(relative)
                rotation = u @ vt
                relative_angle = abs(math.degrees(math.atan2(
                    float(rotation[1, 0]),
                    float(rotation[0, 0]),
                )))
                self.assertLessEqual(relative_angle, limit)

    def test_both_real_pairs_return_coordinate_error_without_mutation(self) -> None:
        for pair_name, (calibration, measurement) in self.pairs.items():
            with self.subTest(pair_name=pair_name):
                original = copy.deepcopy((calibration, measurement))
                result = calculate(
                    calibration,
                    measurement,
                    self.config,
                    self.model,
                    allow_simulation_model=True,
                )
                self.assertEqual("error", result["status"], result)
                self.assertEqual("COORDINATE_SYSTEM_INVALID", result["error"]["code"])
                self.assertEqual("GEOMETRY_INVALID", result["error"]["details"]["reason"])
                self.assertIn("identity-blocking", result["error"]["message"])
                self.assertNotIn("result", result)
                self.assertTrue(validate_result(result).valid)
                self.assertEqual(original, (calibration, measurement))

    def test_single_image_is_usable_does_not_authorize_a_prescription(self) -> None:
        for pair_name, (calibration, measurement) in self.pairs.items():
            with self.subTest(pair_name=pair_name):
                self.assertTrue(calibration["quality"]["is_usable"])
                self.assertTrue(measurement["quality"]["is_usable"])
                result = calculate(
                    calibration,
                    measurement,
                    self.config,
                    self.model,
                    allow_simulation_model=True,
                )
                self.assertEqual("COORDINATE_SYSTEM_INVALID", result["error"]["code"])

    def test_pair_2_without_quality_markers_still_rejects_incomplete_identity_assignment(self) -> None:
        calibration, measurement = copy.deepcopy(self.pairs["pair_2"])
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
        self.assertEqual("error", result["status"], result)
        self.assertEqual("COORDINATE_SYSTEM_INVALID", result["error"]["code"], result)
        self.assertEqual("GEOMETRY_INVALID", result["error"]["details"]["reason"])
        self.assertIn("assigned 23 of 27", result["error"]["message"])
        self.assertNotIn("result", result)


if __name__ == "__main__":
    unittest.main()
