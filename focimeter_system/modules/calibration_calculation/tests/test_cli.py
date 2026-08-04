import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
M2_MOCKS = Path("data/mock/m2_image_recognition")
M3_MOCKS = Path("data/mock/m3_calibration_calculation")


class CliTests(unittest.TestCase):
    def run_cli(self, *args: object) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [sys.executable, "-m", "modules.calibration_calculation.validator.cli", *map(str, args)],
            cwd=ROOT,
            text=True,
            capture_output=True,
            check=False,
        )

    def test_inputs_command_accepts_canonical_examples(self) -> None:
        completed = self.run_cli(
            "inputs",
            "--calibration", M2_MOCKS / "spots_calib_ok.json",
            "--measurement", M2_MOCKS / "spots_meas_ok.json",
            "--config", "config/default_config.json",
            "--mode", "contract",
        )
        self.assertEqual(0, completed.returncode, completed.stderr or completed.stdout)
        self.assertEqual({"valid": True, "issues": []}, json.loads(completed.stdout))

    def test_result_command_accepts_canonical_result(self) -> None:
        completed = self.run_cli("result", "--file", M3_MOCKS / "result_spherical_ok.json")
        self.assertEqual(0, completed.returncode, completed.stderr or completed.stdout)
        self.assertTrue(json.loads(completed.stdout)["valid"])

    def test_missing_config_returns_code_two(self) -> None:
        completed = self.run_cli(
            "inputs",
            "--calibration", M2_MOCKS / "spots_calib_ok.json",
            "--measurement", M2_MOCKS / "spots_meas_ok.json",
            "--config", "missing.json",
        )
        self.assertEqual(2, completed.returncode)
        self.assertEqual("CONFIG_NOT_FOUND", json.loads(completed.stdout)["issues"][0]["code"])

    def test_missing_spot_file_returns_image_not_found(self) -> None:
        completed = self.run_cli(
            "inputs",
            "--calibration", "missing.json",
            "--measurement", M2_MOCKS / "spots_meas_ok.json",
            "--config", "config/default_config.json",
        )
        self.assertEqual(2, completed.returncode)
        self.assertEqual("IMAGE_NOT_FOUND", json.loads(completed.stdout)["issues"][0]["code"])

    def test_malformed_json_returns_config_invalid(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            malformed = Path(directory) / "malformed.json"
            malformed.write_text("{", encoding="utf-8")
            completed = self.run_cli("result", "--file", malformed)
        self.assertEqual(2, completed.returncode)
        self.assertEqual("CONFIG_INVALID", json.loads(completed.stdout)["issues"][0]["code"])


if __name__ == "__main__":
    unittest.main()
