import copy
import json
import unittest
from pathlib import Path

from modules.calibration_calculation.validator.contract_validator import validate_result


ROOT = Path(__file__).resolve().parents[3]
M3_MOCKS = ROOT / "data" / "mock" / "m3_calibration_calculation"


class OutputContractTests(unittest.TestCase):
    def load(self, path: str) -> dict:
        return json.loads((M3_MOCKS / path).read_text(encoding="utf-8"))

    def test_canonical_results_pass(self) -> None:
        self.assertTrue(validate_result(self.load("result_spherical_ok.json")).valid)
        self.assertTrue(validate_result(self.load("result_cylindrical_ok.json")).valid)

    def test_canonical_error_passes(self) -> None:
        self.assertTrue(validate_result(self.load("error_coordinate_invalid.json")).valid)

    def test_nonzero_cylinder_spherical_result_fails(self) -> None:
        result = copy.deepcopy(self.load("result_spherical_ok.json"))
        result["result"]["C"] = -0.25
        self.assertFalse(validate_result(result).valid)

    def test_zero_cylinder_cylindrical_result_fails(self) -> None:
        result = copy.deepcopy(self.load("result_cylindrical_ok.json"))
        result["result"]["C"] = 0.0
        self.assertFalse(validate_result(result).valid)

    def test_wrong_diopter_unit_reports_unit_mismatch(self) -> None:
        result = copy.deepcopy(self.load("result_spherical_ok.json"))
        result["result"]["unit"] = "m-1"
        report = validate_result(result)
        self.assertIn("UNIT_MISMATCH", [issue.code for issue in report.issues])

    def test_nonfinite_sphere_is_rejected(self) -> None:
        result = copy.deepcopy(self.load("result_spherical_ok.json"))
        result["result"]["S"] = float("inf")
        self.assertFalse(validate_result(result).valid)

    def test_success_only_fields_are_rejected_in_error_envelope(self) -> None:
        result = self.load("error_coordinate_invalid.json")
        result["lens_type"] = "spherical"
        self.assertFalse(validate_result(result).valid)


if __name__ == "__main__":
    unittest.main()
