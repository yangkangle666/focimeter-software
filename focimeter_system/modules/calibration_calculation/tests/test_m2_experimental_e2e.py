import copy
import hashlib
import json
import random
import unittest
from pathlib import Path

from modules.calibration_calculation.algorithm.experimental_input import parse_experimental_pair
from modules.calibration_calculation.algorithm.input_preparation import prepare_calculation_inputs
from modules.calibration_calculation.algorithm.multispot_matching import match_experimental_multispot
from modules.calibration_calculation.algorithm.types import CalibrationModel


MODULE_ROOT = Path(__file__).resolve().parents[1]
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

    def test_normal_94_point_outputs_are_uniquely_matched(self):
        prepared = prepare_calculation_inputs(self.calibration, self.measurement, self.model)
        self.assertIsNotNone(prepared.matching)
        self.assertEqual(94, prepared.matching.calibration_detection_count)
        self.assertEqual(94, prepared.matching.measurement_detection_count)
        self.assertEqual(94, prepared.matching.matched_spot_count)
        self.assertEqual(
            set(range(94)),
            {spot["spot_id"] for spot in prepared.calibration["spots"]},
        )

    def test_array_order_and_detection_ids_do_not_define_identity(self):
        baseline = match_experimental_multispot(
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
        matched = match_experimental_multispot(
            parse_experimental_pair(calibration, measurement),
            self.model.matching_limits,
        )
        self.assertEqual(94, matched.diagnostics.matched_spot_count)
        self.assertTrue({item[0] for item in matched.detection_pairs}.isdisjoint({item[1] for item in matched.detection_pairs}))
        baseline_pairs = [
            (c["x"], c["y"], m["x"], m["y"])
            for c, m in zip(baseline.calibration["spots"], baseline.measurement["spots"], strict=True)
        ]
        reordered_pairs = [
            (c["x"], c["y"], m["x"], m["y"])
            for c, m in zip(matched.calibration["spots"], matched.measurement["spots"], strict=True)
        ]
        self.assertEqual(baseline_pairs, reordered_pairs)

    def test_one_side_point_loss_uses_partial_overlap(self):
        measurement = copy.deepcopy(self.measurement)
        measurement["spots"].pop()
        measurement["quality"]["detected_count"] = len(measurement["spots"])
        matched = match_experimental_multispot(
            parse_experimental_pair(self.calibration, measurement),
            self.model.matching_limits,
        )
        self.assertEqual(93, matched.diagnostics.matched_spot_count)
        self.assertEqual(1, matched.diagnostics.unmatched_calibration_count)


if __name__ == "__main__":
    unittest.main()
