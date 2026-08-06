import copy
import json
import unittest
from pathlib import Path

from jsonschema import Draft202012Validator

from modules.calibration_calculation.algorithm.calculator import calculate
from modules.calibration_calculation.algorithm.experimental_input import parse_experimental_pair
from modules.calibration_calculation.algorithm.multispot_matching import (
    match_experimental_multispot,
)
from modules.calibration_calculation.algorithm.types import CalibrationModel
from modules.calibration_calculation.validator.contract_validator import validate_result


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
        cls.schema = json.loads(
            (MODULE_ROOT / "schemas" / "m2_multispot_experimental.schema.json").read_text(
                encoding="utf-8"
            )
        )
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
        self.assertTrue(validate_result(result).valid)
        serialized = json.dumps(result, sort_keys=True)
        for prescription_key in ('"S"', '"C"', '"A"'):
            self.assertNotIn(prescription_key, serialized)

    def test_all_four_documents_conform_to_the_m3_experimental_schema(self) -> None:
        validator = Draft202012Validator(self.schema)
        for pair_name in ("pair_1", "pair_2"):
            for document in self._load_pair(pair_name):
                with self.subTest(pair=pair_name, image_type=document["image_type"]):
                    errors = sorted(
                        validator.iter_errors(document),
                        key=lambda error: list(error.absolute_path),
                    )
                    self.assertEqual([], errors, [error.message for error in errors])
                    self.assertEqual("software_only", document["validation_scope"])
                    self.assertFalse(document["metrology_validated"])
                    self.assertTrue(document["quality"]["is_usable"])
                    expected_count = 31 if document["image_type"] == "calibration" else 27
                    self.assertEqual(expected_count, len(document["spots"]))
                    self.assertFalse(document["matching"]["physical_identity_guaranteed"])
                    self.assertTrue(all("spot_id" not in spot for spot in document["spots"]))

    def test_identity_risk_markers_are_preserved_at_known_locations(self) -> None:
        pair_1_calibration, pair_1_measurement = self._load_pair("pair_1")
        pair_2_calibration, pair_2_measurement = self._load_pair("pair_2")

        def flags_by_detection(document: dict) -> dict[int, set[str]]:
            return {
                int(spot["detection_id"]): set(spot.get("quality_flags", []))
                for spot in document["spots"]
            }

        for calibration in (pair_1_calibration, pair_2_calibration):
            calibration_flags = flags_by_detection(calibration).values()
            self.assertTrue(any("AREA_ABOVE_MEDIAN" in flags for flags in calibration_flags))
            self.assertTrue(
                any(
                    "LATTICE_RECOVERED_UNVERIFIED" in flags
                    for flags in flags_by_detection(calibration).values()
                )
            )
        pair_1_measurement_flags = flags_by_detection(pair_1_measurement)
        self.assertIn("SUBPITCH_FRAGMENT_NEIGHBOR_REJECTED", pair_1_measurement_flags[13])
        self.assertIn("SUBPITCH_FRAGMENT_NEIGHBOR_REJECTED", pair_1_measurement_flags[18])
        self.assertIn(
            "SUBPITCH_FRAGMENT_REJECTED_UNVERIFIED",
            pair_1_measurement["quality"]["warnings"],
        )
        self.assertTrue(
            any(
                "AREA_ABOVE_MEDIAN" in flags
                for flags in flags_by_detection(pair_2_measurement).values()
            )
        )

    def test_single_image_usable_state_does_not_authorize_a_prescription(self) -> None:
        for pair_name in ("pair_1", "pair_2"):
            with self.subTest(pair=pair_name):
                calibration, measurement = self._load_pair(pair_name)
                self.assertTrue(calibration["quality"]["is_usable"])
                self.assertTrue(measurement["quality"]["is_usable"])
                result = calculate(
                    calibration,
                    measurement,
                    self.config,
                    self.model,
                    allow_simulation_model=True,
                )
                self._assert_identity_error(result)

    def test_original_real_pairs_reject_unsafe_retained_spot_flags(self) -> None:
        for pair_name in ("pair_1", "pair_2"):
            with self.subTest(pair=pair_name):
                calibration, measurement = self._load_pair(pair_name)
                original = copy.deepcopy((calibration, measurement))
                result = calculate(
                    calibration,
                    measurement,
                    self.config,
                    self.model,
                    allow_simulation_model=True,
                )

                self._assert_identity_error(result)
                self.assertIn("unsafe quality flags", result["error"]["message"])
                self.assertEqual(original, (calibration, measurement))

    def test_quality_cleared_copies_cover_all_measurements_for_detection_diagnostics(self) -> None:
        for pair_name in ("pair_1", "pair_2"):
            with self.subTest(pair=pair_name):
                calibration, measurement = self._load_pair(pair_name)
                calibration = copy.deepcopy(calibration)
                measurement = copy.deepcopy(measurement)
                # The formal calculation path above must reject these flags. This
                # copy only isolates topology and never produces a prescription.
                for document in (calibration, measurement):
                    document["quality"]["warnings"] = []
                    for spot in document["spots"]:
                        spot["quality_flags"] = []

                matched = match_experimental_multispot(
                    parse_experimental_pair(calibration, measurement),
                    self.model.matching_limits,
                )

                self.assertEqual(31, matched.diagnostics.calibration_detection_count)
                self.assertEqual(27, matched.diagnostics.measurement_detection_count)
                self.assertEqual(27, matched.diagnostics.matched_spot_count)
                self.assertEqual(4, matched.diagnostics.unmatched_calibration_count)
                self.assertEqual(0, matched.diagnostics.unmatched_measurement_count)


if __name__ == "__main__":
    unittest.main()
