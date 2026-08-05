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

    def test_software_only_scope_is_accepted(self) -> None:
        calibration = experimental_document("calibration", [0, 1, 2, 3])
        measurement = experimental_document("measurement", [4, 5, 6, 7])
        calibration["validation_scope"] = "software_only"
        measurement["validation_scope"] = "software_only"
        pair = parse_experimental_pair(calibration, measurement)
        self.assertEqual(4, len(pair.calibration))

    def test_identity_risk_flags_are_rejected_without_filtering_points(self) -> None:
        for flag in ("AREA_ABOVE_MEDIAN", "SUBPITCH_FRAGMENT_NEIGHBOR_REJECTED"):
            with self.subTest(flag=flag):
                calibration = experimental_document("calibration", [0, 1, 2, 3])
                calibration["spots"][1]["quality_flags"] = [flag]
                with self.assertRaisesRegex(CoordinateSystemError, "identity-blocking"):
                    parse_experimental_pair(
                        calibration,
                        experimental_document("measurement", [0, 1, 2, 3]),
                    )

    def test_unverified_subpitch_warning_rejects_the_whole_pair(self) -> None:
        measurement = experimental_document("measurement", [0, 1, 2, 3])
        measurement["quality"]["warnings"] = ["SUBPITCH_FRAGMENT_REJECTED_UNVERIFIED"]
        with self.assertRaisesRegex(CoordinateSystemError, "identity-blocking quality warnings"):
            parse_experimental_pair(experimental_document("calibration", [0, 1, 2, 3]), measurement)

    def test_reviewed_document_warnings_are_accepted(self) -> None:
        calibration = experimental_document("calibration", [0, 1, 2, 3])
        calibration["quality"]["warnings"] = [
            "AREA_VARIATION_HIGH",
            "EDGE_CLIPPED_CANDIDATE_REJECTED",
            "MOCK_DATA_ONLY",
            "NEARBY_FRAGMENT_REJECTED",
            "SATURATED_PEAK",
            "SMALL_AREA_OUTLIER_REJECTED",
        ]
        pair = parse_experimental_pair(
            calibration,
            experimental_document("measurement", [0, 1, 2, 3]),
        )
        self.assertEqual(4, len(pair.calibration))

    def test_unknown_document_warning_is_rejected(self) -> None:
        calibration = experimental_document("calibration", [0, 1, 2, 3])
        calibration["quality"]["warnings"] = ["NEW_UNREVIEWED_WARNING"]
        with self.assertRaisesRegex(CoordinateSystemError, "unreviewed quality warnings"):
            parse_experimental_pair(
                calibration,
                experimental_document("measurement", [0, 1, 2, 3]),
            )

    def test_unknown_quality_flag_is_rejected(self) -> None:
        calibration = experimental_document("calibration", [0, 1, 2, 3])
        calibration["spots"][1]["quality_flags"] = ["EDGE_CLIPPED"]
        with self.assertRaisesRegex(CoordinateSystemError, "unreviewed"):
            parse_experimental_pair(calibration, experimental_document("measurement", [0, 1, 2, 3]))

    def test_input_spot_id_is_rejected_instead_of_silently_ignored(self) -> None:
        calibration = experimental_document("calibration", [0, 1, 2, 3])
        calibration["spots"][0]["spot_id"] = 0
        with self.assertRaisesRegex(ModelError, "Invalid M2 experimental calibration document"):
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
