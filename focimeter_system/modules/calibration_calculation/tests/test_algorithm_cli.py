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

    def test_real_multispot_engineering_output_requires_explicit_flag(self) -> None:
        pair_root = (
            ROOT
            / "modules/image_recognition/samples/real_jpeg_software_verified/pair_2"
        )
        common = (
            "calculate",
            "--calibration", pair_root / "spots_calib_multispot.json",
            "--measurement", pair_root / "spots_meas_multispot.json",
            "--config", ROOT / "config/default_config.json",
            "--model", self.model_path,
        )

        denied = self.run_cli(*common, "--allow-simulation-model")
        self.assertEqual(2, denied.returncode, denied.stderr or denied.stdout)
        self.assertEqual("COORDINATE_SYSTEM_INVALID", json.loads(denied.stdout)["error"]["code"])

        allowed = self.run_cli(*common, "--engineering-mode")
        self.assertEqual(0, allowed.returncode, allowed.stderr or allowed.stdout)
        payload = json.loads(allowed.stdout)
        self.assertEqual("spherical", payload["lens_type"])
        self.assertEqual(0, payload["result"]["C"])
        self.assertIsNone(payload["result"]["A"])
        self.assertEqual("software_only", payload["quality"]["validation_scope"])
        self.assertFalse(payload["quality"]["metrology_validated"])

    def test_calibration_artifact_uses_independent_final_test(self) -> None:
        cases = [
            ("sphere_neg_5", "train", Prescription(-5, 0, None), "SPH-N05", (0, 0)),
            ("sphere_pos_5", "train", Prescription(5, 0, None), "SPH-P05", (0, 0)),
            ("cyl_axis_0", "train", Prescription(0, -1.5, 0), "CYL-N15", (0, 0)),
            ("cyl_axis_45", "train", Prescription(0, -1.5, 45), "CYL-N15", (0, 0)),
            ("zero_1", "validation", Prescription(0, 0, None), "ZERO-VALIDATION", (0, 0)),
            ("zero_2", "validation", Prescription(0, 0, None), "ZERO-VALIDATION", (0, 0)),
            ("sphere_pos_2_5", "validation", Prescription(2.5, 0, None), "SPH-P025-VALIDATION", (0, 0)),
            ("cyl_axis_90_1", "validation", Prescription(0, -1.5, 90), "CYL-N15-VALIDATION", (0, 0)),
            ("cyl_axis_90_2", "validation", Prescription(0, -1.5, 90), "CYL-N15-VALIDATION", (0, 0)),
            ("zero_test_1", "test", Prescription(0, 0, None), "ZERO-TEST", (10, 0)),
            ("zero_test_2", "test", Prescription(0, 0, None), "ZERO-TEST", (11, 0)),
            ("sphere_pos_2_5_test", "test", Prescription(2.5, 0, None), "SPH-P025-TEST", (12, 0)),
            ("cyl_axis_90_test_1", "test", Prescription(0, -1.5, 90), "CYL-N15-TEST", (13, 0)),
            ("cyl_axis_90_test_2", "test", Prescription(0, -1.5, 90), "CYL-N15-TEST", (14, 0)),
        ]
        samples = []
        for sample_id, partition, prescription, serial, translation in cases:
            relative = f"data/{sample_id}.json"
            matrix = power_vector_to_matrix(prescription_to_power_vector(prescription))
            transform = np.eye(2) - self.config["optical"]["distance_m"] * matrix
            measurement = transformed_measurement(self.calibration, transform, translation)
            (self.project / relative).write_text(json.dumps(measurement), encoding="utf-8")
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
        self.assertEqual([0.0, 2.5], model["quality_limits"]["validated_sphere_range_D"])
        self.assertEqual(1.5, model["quality_limits"]["validated_abs_cylinder_max_D"])
        self.assertEqual("validation", model["fit_metrics"]["validation"]["evaluation_partition"])
        self.assertEqual("test", model["fit_metrics"]["final_test"]["evaluation_partition"])

        duplicate_id = copy.deepcopy(samples)
        duplicate_id[1]["sample_id"] = duplicate_id[0]["sample_id"]
        duplicate_id_path = self.project / "duplicate_id_dataset.json"
        duplicate_id_path.write_text(
            json.dumps({"schema_version": "1.0", "dataset_id": "duplicate-id", "data_kind": "simulation", "samples": duplicate_id}),
            encoding="utf-8",
        )
        duplicate_id_result = self.run_cli(
            "fit-model", "--dataset", duplicate_id_path, "--config", self.config_path,
            "--project-root", self.project, "--output", self.project / "duplicate_id_model.json",
        )
        self.assertEqual(2, duplicate_id_result.returncode, duplicate_id_result.stderr or duplicate_id_result.stdout)
        self.assertIn("sample_id values must be unique", json.loads(duplicate_id_result.stdout)["error"]["message"])

        reused_measurement = copy.deepcopy(samples)
        reused_measurement[4]["spots_meas_path"] = reused_measurement[0]["spots_meas_path"]
        reused_measurement_path = self.project / "reused_measurement_dataset.json"
        reused_measurement_path.write_text(
            json.dumps({"schema_version": "1.0", "dataset_id": "reused-measurement", "data_kind": "simulation", "samples": reused_measurement}),
            encoding="utf-8",
        )
        reused_measurement_result = self.run_cli(
            "fit-model", "--dataset", reused_measurement_path, "--config", self.config_path,
            "--project-root", self.project, "--output", self.project / "reused_measurement_model.json",
        )
        self.assertEqual(2, reused_measurement_result.returncode, reused_measurement_result.stderr or reused_measurement_result.stdout)
        self.assertIn("Measurement paths must not cross", json.loads(reused_measurement_result.stdout)["error"]["message"])

        copied_measurement = copy.deepcopy(samples)
        copied_relative = "data/copied_measurement.json"
        (self.project / copied_relative).write_text(
            (self.project / copied_measurement[0]["spots_meas_path"]).read_text(encoding="utf-8"),
            encoding="utf-8",
        )
        copied_measurement[4]["spots_meas_path"] = copied_relative
        copied_measurement_path = self.project / "copied_measurement_dataset.json"
        copied_measurement_path.write_text(
            json.dumps({"schema_version": "1.0", "dataset_id": "copied-measurement", "data_kind": "simulation", "samples": copied_measurement}),
            encoding="utf-8",
        )
        copied_measurement_result = self.run_cli(
            "fit-model", "--dataset", copied_measurement_path, "--config", self.config_path,
            "--project-root", self.project, "--output", self.project / "copied_measurement_model.json",
        )
        self.assertEqual(2, copied_measurement_result.returncode, copied_measurement_result.stderr or copied_measurement_result.stdout)
        self.assertIn("Measurement content must not cross", json.loads(copied_measurement_result.stdout)["error"]["message"])

        reused_serial = copy.deepcopy(samples)
        reused_serial[9]["serial_number"] = reused_serial[4]["serial_number"]
        reused_serial_path = self.project / "reused_serial_dataset.json"
        reused_serial_path.write_text(
            json.dumps({"schema_version": "1.0", "dataset_id": "reused-serial", "data_kind": "simulation", "samples": reused_serial}),
            encoding="utf-8",
        )
        reused_serial_result = self.run_cli(
            "fit-model", "--dataset", reused_serial_path, "--config", self.config_path,
            "--project-root", self.project, "--output", self.project / "reused_serial_model.json",
        )
        self.assertEqual(2, reused_serial_result.returncode, reused_serial_result.stderr or reused_serial_result.stdout)
        self.assertIn("serial numbers must not cross", json.loads(reused_serial_result.stdout)["error"]["message"])


if __name__ == "__main__":
    unittest.main()
