import copy
import json
import unittest
from pathlib import Path

from jsonschema import Draft202012Validator


ROOT = Path(__file__).resolve().parents[3]
MODULE_ROOT = ROOT / "modules" / "calibration_calculation"
SCHEMA_ROOT = MODULE_ROOT / "schemas"
M2_MOCKS = ROOT / "data" / "mock" / "m2_image_recognition"
M3_MOCKS = ROOT / "data" / "mock" / "m3_calibration_calculation"


class SchemaContractTests(unittest.TestCase):
    def load_json(self, path: Path) -> dict:
        return json.loads(path.read_text(encoding="utf-8"))

    def validator(self, schema_name: str) -> Draft202012Validator:
        schema = json.loads((SCHEMA_ROOT / schema_name).read_text(encoding="utf-8"))
        Draft202012Validator.check_schema(schema)
        return Draft202012Validator(schema)

    def assert_schema_valid(self, schema_name: str, data: dict) -> None:
        errors = sorted(self.validator(schema_name).iter_errors(data), key=lambda error: list(error.path))
        self.assertEqual([], errors, [error.message for error in errors])

    def assert_schema_invalid(self, schema_name: str, data: dict) -> None:
        self.assertTrue(list(self.validator(schema_name).iter_errors(data)))

    def test_canonical_spot_examples_match_schema(self) -> None:
        calibration = self.load_json(M2_MOCKS / "spots_calib_ok.json")
        measurement = self.load_json(M2_MOCKS / "spots_meas_ok.json")
        self.assert_schema_valid("spot_result.schema.json", calibration)
        self.assert_schema_valid("spot_result.schema.json", measurement)

    def test_canonical_configuration_and_frontend_match_schemas(self) -> None:
        config = json.loads((ROOT / "config" / "default_config.json").read_text(encoding="utf-8"))
        frontend = self.load_json(M3_MOCKS / "frontend_add_input_template.json")
        self.assert_schema_valid("config.schema.json", config)
        self.assert_schema_valid("frontend_input.schema.json", frontend)

    def test_canonical_outputs_match_schemas(self) -> None:
        self.assert_schema_valid(
            "result_success.schema.json", self.load_json(M3_MOCKS / "result_spherical_ok.json")
        )
        self.assert_schema_valid(
            "result_success.schema.json", self.load_json(M3_MOCKS / "result_cylindrical_ok.json")
        )
        self.assert_schema_valid(
            "error.schema.json", self.load_json(M3_MOCKS / "error_coordinate_invalid.json")
        )

    def test_camel_case_task_id_is_rejected(self) -> None:
        data = self.load_json(M2_MOCKS / "spots_calib_ok.json")
        data["taskId"] = data.pop("task_id")
        self.assert_schema_invalid("spot_result.schema.json", data)

    def test_spherical_axis_is_rejected(self) -> None:
        data = copy.deepcopy(self.load_json(M3_MOCKS / "result_spherical_ok.json"))
        data["result"]["A"] = 90.0
        self.assert_schema_invalid("result_success.schema.json", data)

    def test_zero_cylinder_is_rejected(self) -> None:
        data = copy.deepcopy(self.load_json(M3_MOCKS / "result_cylindrical_ok.json"))
        data["result"]["C"] = 0.0
        self.assert_schema_invalid("result_success.schema.json", data)


if __name__ == "__main__":
    unittest.main()
