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

    def test_expected_count_mismatch_is_rejected(self) -> None:
        self.config["recognition"]["expected_spot_count"] = 6
        self.assert_issue(validate_inputs(self.calibration, self.measurement, self.config), "SPOT_COUNT_MISMATCH")

    def test_missing_axis_role_is_rejected(self) -> None:
        self.calibration["spots"][4]["role"] = "other"
        self.assert_issue(validate_inputs(self.calibration, self.measurement, self.config), "COORDINATE_SYSTEM_INVALID")

    def test_duplicate_center_role_is_rejected(self) -> None:
        self.calibration["spots"][3]["role"] = "center"
        self.assert_issue(validate_inputs(self.calibration, self.measurement, self.config), "COORDINATE_SYSTEM_INVALID")

    def test_duplicate_spot_id_is_rejected(self) -> None:
        self.calibration["spots"][1]["spot_id"] = 0
        self.assert_issue(validate_inputs(self.calibration, self.measurement, self.config), "CONFIG_INVALID")

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
