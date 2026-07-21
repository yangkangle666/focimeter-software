import copy
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

import numpy as np

from modules.calibration_calculation.algorithm.power_vector import power_vector_to_matrix, prescription_to_power_vector
from modules.calibration_calculation.algorithm.types import Prescription
from modules.calibration_calculation.tests.test_geometry import transformed_measurement


ROOT = Path(__file__).resolve().parents[3]
MODULE_ROOT = ROOT / "modules" / "calibration_calculation"


class AlgorithmCliTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.project = Path(self.temporary.name)
        (self.project / "data").mkdir()
        self.config = json.loads((ROOT / "config/default_config.json").read_text(encoding="utf-8"))
        self.calibration = json.loads(
            (ROOT / "data/mock/m2_image_recognition/spots_calib_ok.json").read_text(encoding="utf-8")
        )
        self.config_path = self.project / "config.json"
        self.calibration_path = self.project / "data/calibration.json"
        self.model_path = self.project / "simulation_model.json"
        self.config_path.write_text(json.dumps(self.config), encoding="utf-8")
        self.calibration_path.write_text(json.dumps(self.calibration), encoding="utf-8")
        self.model_path.write_text(
            (MODULE_ROOT / "examples/calibration/calibration_model.simulation.json").read_text(encoding="utf-8"),
            encoding="utf-8",
        )

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def measurement_for(self, prescription: Prescription) -> dict:
        matrix = power_vector_to_matrix(prescription_to_power_vector(prescription))
        transform = np.eye(2) - self.config["optical"]["distance_m"] * matrix
        return transformed_measurement(self.calibration, transform)

    def run_cli(self, *args: object) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [sys.executable, "-m", "modules.calibration_calculation.algorithm.cli", *map(str, args)],
            cwd=ROOT,
            text=True,
            capture_output=True,
            check=False,
        )

    def test_calculate_requires_explicit_simulation_flag(self) -> None:
        measurement_path = self.project / "data/measurement.json"
        measurement_path.write_text(json.dumps(self.measurement_for(Prescription(-2.5, 0, None))), encoding="utf-8")
        common = (
            "calculate", "--calibration", self.calibration_path,
            "--measurement", measurement_path, "--config", self.config_path,
            "--model", self.model_path,
        )
        denied = self.run_cli(*common)
        self.assertEqual(2, denied.returncode, denied.stderr or denied.stdout)
        self.assertEqual("CONFIG_INVALID", json.loads(denied.stdout)["error"]["code"])
        allowed = self.run_cli(*common, "--allow-simulation-model")
        self.assertEqual(0, allowed.returncode, allowed.stderr or allowed.stdout)
        self.assertAlmostEqual(-2.5, json.loads(allowed.stdout)["result"]["S"])

    def test_fit_model_writes_simulation_status(self) -> None:
        cases = [
            ("sphere_neg_5", "train", Prescription(-5, 0, None), "SPH-N05"),
            ("sphere_pos_5", "train", Prescription(5, 0, None), "SPH-P05"),
            ("cyl_axis_0", "train", Prescription(0, -1.5, 0), "CYL-N15"),
            ("cyl_axis_45", "train", Prescription(0, -1.5, 45), "CYL-N15"),
            ("zero_1", "validation", Prescription(0, 0, None), "ZERO"),
            ("zero_2", "validation", Prescription(0, 0, None), "ZERO"),
            ("sphere_pos_2_5", "validation", Prescription(2.5, 0, None), "SPH-P025"),
            ("cyl_axis_90_1", "validation", Prescription(0, -1.5, 90), "CYL-N15-A90"),
            ("cyl_axis_90_2", "validation", Prescription(0, -1.5, 90), "CYL-N15-A90"),
        ]
        samples = []
        for sample_id, partition, prescription, serial in cases:
            relative = f"data/{sample_id}.json"
            (self.project / relative).write_text(json.dumps(self.measurement_for(prescription)), encoding="utf-8")
            samples.append(
                {
                    "sample_id": sample_id,
                    "partition": partition,
                    "serial_number": serial,
                    "spots_calib_path": "data/calibration.json",
                    "spots_meas_path": relative,
                    "certified": {
                        "S": prescription.S, "C": prescription.C, "A": prescription.A,
                        "unit": "D", "angle_unit": "degree", "notation": "minus_cylinder"
                    },
                    "uncertainty_D": 0.015 if prescription.C else 0.03,
                }
            )
        dataset_path = self.project / "dataset.json"
        output_path = self.project / "fitted_model.json"
        dataset_path.write_text(
            json.dumps({"schema_version": "1.0", "dataset_id": "synthetic", "data_kind": "simulation", "samples": samples}),
            encoding="utf-8",
        )
        completed = self.run_cli(
            "fit-model", "--dataset", dataset_path, "--config", self.config_path,
            "--project-root", self.project, "--output", output_path,
        )
        self.assertEqual(0, completed.returncode, completed.stderr or completed.stdout)
        model = json.loads(output_path.read_text(encoding="utf-8"))
        self.assertEqual("simulation_only", model["validation_status"])
        np.testing.assert_allclose(np.eye(3), model["correction"]["matrix"], atol=1e-10)


if __name__ == "__main__":
    unittest.main()
