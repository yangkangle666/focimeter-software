import hashlib
import json
import unittest
from pathlib import Path

from jsonschema import Draft202012Validator


ROOT = Path(__file__).resolve().parents[3]
FIXTURE_ROOT = ROOT / "data" / "real" / "multispot_lens_pairs" / "real_lens_pair_set_001"
SCHEMA_ROOT = ROOT / "modules" / "calibration_calculation" / "schemas"


class RealMultispotFixtureTests(unittest.TestCase):
    def load_json(self, path: Path) -> dict:
        return json.loads(path.read_text(encoding="utf-8"))

    def sha256(self, path: Path) -> str:
        digest = hashlib.sha256()
        with path.open("rb") as file:
            for chunk in iter(lambda: file.read(1024 * 1024), b""):
                digest.update(chunk)
        return digest.hexdigest()

    def test_manifest_file_hashes_match_images(self) -> None:
        manifest = self.load_json(FIXTURE_ROOT / "manifest.json")

        self.assertEqual("real", manifest["data_source"])
        self.assertFalse(manifest["metrology_validated"])
        self.assertEqual("raw_received", manifest["validation_status"])
        for item in manifest["files"]:
            path = FIXTURE_ROOT / item["path"]
            self.assertTrue(path.is_file(), item)
            self.assertEqual(item["sha256"], self.sha256(path), item)

    def test_cases_record_device_reference_only_values(self) -> None:
        manifest = self.load_json(FIXTURE_ROOT / "manifest.json")
        cases = {item["case_id"]: item for item in manifest["cases"]}

        self.assertEqual(-5.25, cases["lens_001"]["device_reference"]["S_D"])
        self.assertEqual(-2.0, cases["lens_001"]["device_reference"]["C_D"])
        self.assertEqual(154, cases["lens_001"]["device_reference"]["A_degree"])
        self.assertEqual(-1.5, cases["lens_002"]["device_reference"]["S_D"])
        self.assertEqual(0.0, cases["lens_002"]["device_reference"]["C_D"])
        for case in cases.values():
            self.assertEqual("device_reference_only", case["device_reference"]["status"])
            self.assertEqual("images/reference_no_lens.jpg", case["calibration_image"])

    def test_m2_input_packages_reference_existing_fixture_files(self) -> None:
        for package_path in sorted((FIXTURE_ROOT / "packages").glob("input_package_*.json")):
            package = self.load_json(package_path)
            self.assertEqual("real", package["data_source"])
            self.assertIn("NOT_METROLOGY_VALIDATED", package["quality"]["warnings"])
            for field in ("calibration_image", "measurement_image", "config_path"):
                path = ROOT / package["data"][field]
                self.assertTrue(path.is_file(), f"{package_path.name}:{field}")

    def test_detection_config_matches_config_schema_but_is_not_calculation_ready(self) -> None:
        config = self.load_json(FIXTURE_ROOT / "config" / "detection_config.json")
        schema = self.load_json(SCHEMA_ROOT / "config.schema.json")
        errors = list(Draft202012Validator(schema).iter_errors(config))

        self.assertEqual([], errors, [error.message for error in errors])
        self.assertIsNone(config["optical"]["distance_m"])
        self.assertIsNone(config["camera"]["pixel_size_um"])


if __name__ == "__main__":
    unittest.main()
