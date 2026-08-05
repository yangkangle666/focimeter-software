import copy
import hashlib
import json
import random
import unittest
from pathlib import Path

from modules.calibration_calculation.algorithm.experimental_input import parse_experimental_pair
from modules.calibration_calculation.algorithm.input_preparation import prepare_calculation_inputs
from modules.calibration_calculation.algorithm.multispot_matching import match_experimental_multispot
from modules.calibration_calculation.algorithm.types import CalibrationModel, CoordinateSystemError


MODULE_ROOT = Path(__file__).resolve().parents[1]
PROJECT_ROOT = Path(__file__).resolve().parents[3]
FIXTURE_ROOT = Path(__file__).resolve().parent / "fixtures" / "m2_experimental_94"


class M2ExperimentalEndToEndTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.manifest = json.loads((FIXTURE_ROOT / "manifest.json").read_text(encoding="utf-8"))
        cls.calibration = json.loads((FIXTURE_ROOT / "spots_calib_multispot.json").read_text(encoding="utf-8"))
        cls.measurement = json.loads((FIXTURE_ROOT / "spots_meas_multispot.json").read_text(encoding="utf-8"))
        cls.model = CalibrationModel.from_dict(json.loads(
            (MODULE_ROOT / "examples" / "calibration" / "calibration_model.simulation.json").read_text(encoding="utf-8")
        ))

    def test_fixed_m2_outputs_match_manifest_hashes(self):
        for name, metadata in self.manifest["files"].items():
            canonical_bytes = (FIXTURE_ROOT / name).read_bytes().replace(b"\r\n", b"\n")
            digest = hashlib.sha256(canonical_bytes).hexdigest()
            self.assertEqual(metadata["sha256"], digest)
        self.assertFalse(self.manifest["metrology_validated"])

    def test_fixture_provenance_identifies_reachable_m2_tree_and_input(self):
        self.assertEqual(40, len(self.manifest["source_commit"]))
        self.assertEqual(40, len(self.manifest["m2_source_tree_sha"]))
        self.assertIn("--experimental-multispot", self.manifest["generation_command"])
        source_input = PROJECT_ROOT / self.manifest["source_input"]
        canonical_bytes = source_input.read_bytes().replace(b"\r\n", b"\n")
        self.assertEqual(
            self.manifest["source_input_sha256_lf"],
            hashlib.sha256(canonical_bytes).hexdigest(),
        )

    def test_94_point_outputs_reject_unmarked_symmetry(self):
        with self.assertRaisesRegex(CoordinateSystemError, "rotation_180"):
            prepare_calculation_inputs(self.calibration, self.measurement, self.model)

    def test_array_order_and_detection_ids_cannot_resolve_symmetry(self):
        with self.assertRaisesRegex(CoordinateSystemError, "rotation_180"):
            match_experimental_multispot(
                parse_experimental_pair(self.calibration, self.measurement),
                self.model.matching_limits,
            )
        calibration = copy.deepcopy(self.calibration)
        measurement = copy.deepcopy(self.measurement)
        random.Random(20260804).shuffle(calibration["spots"])
        random.Random(42).shuffle(measurement["spots"])
        for index, spot in enumerate(calibration["spots"]):
            spot["detection_id"] = 5000 + index
        for index, spot in enumerate(measurement["spots"]):
            spot["detection_id"] = 9000 + index
        with self.assertRaisesRegex(CoordinateSystemError, "rotation_180"):
            match_experimental_multispot(
                parse_experimental_pair(calibration, measurement),
                self.model.matching_limits,
            )

    def test_one_side_point_loss_rejects_the_whole_pair(self):
        measurement = copy.deepcopy(self.measurement)
        measurement["spots"].pop()
        measurement["quality"]["detected_count"] = len(measurement["spots"])
        with self.assertRaisesRegex(CoordinateSystemError, "Every detected spot"):
            match_experimental_multispot(
                parse_experimental_pair(self.calibration, measurement),
                self.model.matching_limits,
            )


if __name__ == "__main__":
    unittest.main()
