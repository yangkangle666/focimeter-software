import copy
import unittest

from modules.calibration_calculation.algorithm.experimental_input import parse_experimental_pair
from modules.calibration_calculation.algorithm.types import CoordinateSystemError, ModelError


def experimental_document(image_type: str, detection_ids: list[int]) -> dict:
    spots = [
        {
            "detection_id": detection_id,
            "x": 100.0 + index * 10.0,
            "y": 200.0 + index * 5.0,
            "confidence": 0.9,
            "quality_flags": [],
        }
        for index, detection_id in enumerate(detection_ids)
    ]
    return {
        "schema_version": "m2.multispot.experimental.1",
        "task_id": "experimental_adapter_test",
        "module": "m2_image_recognition",
        "status": "ok",
        "experimental": True,
        "contract_status": "proposed",
        "data_source": "synthetic",
        "validation_status": "software_verified",
        "validation_scope": "simulation_only",
        "metrology_validated": False,
        "image_type": image_type,
        "coordinate_type": "image_pixel",
        "spots": spots,
        "quality": {"detected_count": len(spots), "is_usable": True, "warnings": []},
        "matching": {
            "status": "not_performed",
            "id_scope": "image_local",
            "physical_identity_guaranteed": False,
            "owner_status": "unassigned",
        },
        "error": None,
    }


class ExperimentalInputTests(unittest.TestCase):
    def test_detection_ids_remain_image_local(self) -> None:
        calibration = experimental_document("calibration", [91, 7, 42, 13])
        measurement = experimental_document("measurement", [4, 88, 2, 71])
        pair = parse_experimental_pair(calibration, measurement)
        self.assertEqual([91, 7, 42, 13], [item.detection_id for item in pair.calibration])
        self.assertEqual([4, 88, 2, 71], [item.detection_id for item in pair.measurement])
        self.assertFalse(hasattr(pair.calibration[0], "spot_id"))

    def test_nonempty_quality_flags_are_rejected(self) -> None:
        calibration = experimental_document("calibration", [0, 1, 2, 3])
        calibration["spots"][1]["quality_flags"] = ["EDGE_CLIPPED"]
        with self.assertRaises(CoordinateSystemError):
            parse_experimental_pair(calibration, experimental_document("measurement", [0, 1, 2, 3]))

    def test_saturated_peak_flag_preserves_high_confidence_detection(self) -> None:
        calibration = experimental_document("calibration", [0, 1, 2, 3])
        measurement = experimental_document("measurement", [10, 11, 12, 13])
        measurement["spots"][1]["quality_flags"] = ["SATURATED_PEAK"]
        pair = parse_experimental_pair(calibration, measurement)
        self.assertEqual(4, len(pair.measurement))

    def test_duplicate_detection_id_is_rejected(self) -> None:
        calibration = experimental_document("calibration", [0, 0, 2, 3])
        with self.assertRaises(ModelError):
            parse_experimental_pair(calibration, experimental_document("measurement", [0, 1, 2, 3]))

    def test_detected_count_must_match_array(self) -> None:
        calibration = experimental_document("calibration", [0, 1, 2, 3])
        calibration["quality"]["detected_count"] = 5
        with self.assertRaises(ModelError):
            parse_experimental_pair(calibration, experimental_document("measurement", [0, 1, 2, 3]))

    def test_task_ids_must_match(self) -> None:
        calibration = experimental_document("calibration", [0, 1, 2, 3])
        measurement = experimental_document("measurement", [0, 1, 2, 3])
        measurement["task_id"] = "different"
        with self.assertRaises(ModelError):
            parse_experimental_pair(calibration, measurement)

    def test_input_documents_are_not_modified(self) -> None:
        calibration = experimental_document("calibration", [9, 2, 8, 1])
        measurement = experimental_document("measurement", [4, 3, 7, 6])
        original = copy.deepcopy((calibration, measurement))
        parse_experimental_pair(calibration, measurement)
        self.assertEqual(original, (calibration, measurement))


if __name__ == "__main__":
    unittest.main()
