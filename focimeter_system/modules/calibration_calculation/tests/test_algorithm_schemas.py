import copy
import json
import unittest
from pathlib import Path

from jsonschema import Draft202012Validator

from modules.calibration_calculation.algorithm.types import CalibrationModel, ModelError


ROOT = Path(__file__).resolve().parents[3]
MODULE_ROOT = ROOT / "modules" / "calibration_calculation"


class AlgorithmSchemaTests(unittest.TestCase):
    def setUp(self) -> None:
        examples = MODULE_ROOT / "examples" / "calibration"
        self.dataset = json.loads((examples / "calibration_dataset.example.json").read_text(encoding="utf-8"))
        self.model = json.loads((examples / "calibration_model.simulation.json").read_text(encoding="utf-8"))

    def validator(self, schema_name: str) -> Draft202012Validator:
        schema = json.loads((MODULE_ROOT / "schemas" / schema_name).read_text(encoding="utf-8"))
        Draft202012Validator.check_schema(schema)
        return Draft202012Validator(schema)

    def assert_valid(self, schema_name: str, data: dict) -> None:
        errors = list(self.validator(schema_name).iter_errors(data))
        self.assertEqual([], errors, [error.message for error in errors])

    def test_examples_match_schemas(self) -> None:
        self.assert_valid("calibration_dataset.schema.json", self.dataset)
        self.assert_valid("calibration_model.schema.json", self.model)

    def test_dataset_requires_explicit_partition(self) -> None:
        dataset = copy.deepcopy(self.dataset)
        dataset["samples"][0].pop("partition")
        self.assertTrue(list(self.validator("calibration_dataset.schema.json").iter_errors(dataset)))

    def test_model_type_round_trip(self) -> None:
        parsed = CalibrationModel.from_dict(self.model)
        self.assertEqual(self.model, parsed.to_dict())

    def test_nonfinite_model_coefficient_is_rejected(self) -> None:
        model = copy.deepcopy(self.model)
        model["correction"]["matrix"][0][0] = float("nan")
        with self.assertRaises(ModelError):
            CalibrationModel.from_dict(model)


if __name__ == "__main__":
    unittest.main()
