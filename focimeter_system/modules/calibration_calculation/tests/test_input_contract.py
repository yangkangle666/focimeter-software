import copy
import json
import unittest
from pathlib import Path

from modules.calibration_calculation.validator.contract_validator import validate_inputs


ROOT = Path(__file__).resolve().parents[3]
M2_MOCKS = ROOT / "data" / "mock" / "m2_image_recognition"


class InputContractTests(unittest.TestCase):
    def setUp(self) -> None:
        self.calibration = self.load("spots_calib_ok.json")
        self.measurement = self.load("spots_meas_ok.json")
        self.config = json.loads((ROOT / "config" / "default_config.json").read_text(encoding="utf-8"))

    def load(self, path: str) -> dict:
        return json.loads((M2_MOCKS / path).read_text(encoding="utf-8"))

    def assert_issue(self, report, code: str) -> None:
        self.assertIn(code, [issue.code for issue in report.issues], report.to_dict())

    def test_valid_mock_inputs_pass_contract_mode(self) -> None:
        report = validate_inputs(self.calibration, self.measurement, self.config)
        self.assertTrue(report.valid, report.to_dict())

    def test_calculation_ready_accepts_unused_unknown_hardware_parameter(self) -> None:
        report = validate_inputs(self.calibration, self.measurement, self.config, mode="calculation-ready")
        self.assertTrue(report.valid, report.to_dict())

    def test_calculation_ready_rejects_unknown_distance(self) -> None:
        self.config["optical"]["distance_m"] = None
        report = validate_inputs(self.calibration, self.measurement, self.config, mode="calculation-ready")
        self.assert_issue(report, "CONFIG_INVALID")
        self.assertIn("config.optical.distance_m", [issue.path for issue in report.issues])

    def test_task_id_mismatch_is_rejected(self) -> None:
        self.measurement["task_id"] = "different"
        self.assert_issue(validate_inputs(self.calibration, self.measurement, self.config), "CONFIG_INVALID")

    def test_detected_count_mismatch_is_rejected(self) -> None:
        self.measurement["quality"]["detected_count"] = 4
        self.assert_issue(validate_inputs(self.calibration, self.measurement, self.config), "SPOT_COUNT_MISMATCH")

    def test_actual_spot_count_below_minimum_is_rejected(self) -> None:
        for document in (self.calibration, self.measurement):
            document["spots"] = document["spots"][:3]
            document["quality"]["detected_count"] = 3
        report = validate_inputs(self.calibration, self.measurement, self.config, mode="calculation-ready")
        self.assert_issue(report, "SPOT_COUNT_MISMATCH")

    def test_expected_count_mismatch_is_rejected(self) -> None:
        self.config["recognition"]["spot_count_mode"] = "fixed"
        self.config["recognition"]["expected_spot_count"] = 6
        self.assert_issue(validate_inputs(self.calibration, self.measurement, self.config), "SPOT_COUNT_MISMATCH")

    def test_axis_roles_are_optional_for_multispot_contract(self) -> None:
        self.calibration["spots"][4]["role"] = "other"
        self.assertTrue(validate_inputs(self.calibration, self.measurement, self.config).valid)

    def test_duplicate_roles_are_allowed_for_multispot_contract(self) -> None:
        self.calibration["spots"][3]["role"] = "center"
        self.measurement["spots"][3]["role"] = "center"
        self.assertTrue(validate_inputs(self.calibration, self.measurement, self.config).valid)

    def test_unknown_role_is_allowed_when_spot_ids_pair(self) -> None:
        self.measurement["spots"][3]["role"] = "unknown"
        report = validate_inputs(self.calibration, self.measurement, self.config, mode="calculation-ready")
        self.assertTrue(report.valid, report.to_dict())

    def test_duplicate_non_axis_role_is_allowed_for_multispot_calculation(self) -> None:
        self.measurement["spots"][3]["role"] = "left_or_negative"
        self.calibration["spots"][3]["role"] = "left_or_negative"
        report = validate_inputs(self.calibration, self.measurement, self.config, mode="calculation-ready")
        self.assertTrue(report.valid, report.to_dict())

    def test_multispot_inputs_without_roles_are_accepted(self) -> None:
        for document in (self.calibration, self.measurement):
            for spot in document["spots"]:
                spot.pop("role")
            base_id = max(spot["spot_id"] for spot in document["spots"]) + 1
            for offset in range(4):
                document["spots"].append(
                    {
                        "spot_id": base_id + offset,
                        "x": 640.0 + offset * 15.0,
                        "y": 500.0 + offset * 10.0,
                        "confidence": 0.9,
                    }
                )
            document["quality"]["detected_count"] = len(document["spots"])
            document["quality"]["expected_count"] = len(document["spots"])
        report = validate_inputs(self.calibration, self.measurement, self.config, mode="calculation-ready")
        self.assertTrue(report.valid, report.to_dict())

    def test_duplicate_spot_id_is_rejected(self) -> None:
        self.calibration["spots"][1]["spot_id"] = 0
        self.assert_issue(validate_inputs(self.calibration, self.measurement, self.config), "CONFIG_INVALID")

    def test_cross_image_spot_id_set_mismatch_is_rejected_for_calculation(self) -> None:
        self.measurement["spots"][1]["spot_id"] = 99
        report = validate_inputs(self.calibration, self.measurement, self.config, mode="calculation-ready")
        self.assert_issue(report, "COORDINATE_SYSTEM_INVALID")

    def test_cross_image_spot_id_must_preserve_role(self) -> None:
        first = self.measurement["spots"][1]
        second = self.measurement["spots"][4]
        first["spot_id"], second["spot_id"] = second["spot_id"], first["spot_id"]
        report = validate_inputs(self.calibration, self.measurement, self.config, mode="calculation-ready")
        self.assert_issue(report, "COORDINATE_SYSTEM_INVALID")

    def test_coordinate_unit_change_is_rejected(self) -> None:
        self.measurement["coordinate_type"] = "millimeter"
        self.assert_issue(validate_inputs(self.calibration, self.measurement, self.config), "UNIT_MISMATCH")

    def test_nonfinite_coordinate_is_rejected(self) -> None:
        self.measurement["spots"][0]["x"] = float("nan")
        self.assert_issue(validate_inputs(self.calibration, self.measurement, self.config), "CONFIG_INVALID")

    def test_todo_confirm_is_allowed_for_unused_spacing_in_ready_mode(self) -> None:
        self.config["optical"]["hartmann_spacing_mm"] = "TODO_CONFIRM"
        self.assertTrue(validate_inputs(self.calibration, self.measurement, self.config).valid)
        self.assertTrue(
            validate_inputs(self.calibration, self.measurement, self.config, mode="calculation-ready").valid
        )

    def test_inputs_are_not_modified(self) -> None:
        original = copy.deepcopy((self.calibration, self.measurement, self.config))
        validate_inputs(self.calibration, self.measurement, self.config)
        self.assertEqual(original, (self.calibration, self.measurement, self.config))


if __name__ == "__main__":
    unittest.main()
