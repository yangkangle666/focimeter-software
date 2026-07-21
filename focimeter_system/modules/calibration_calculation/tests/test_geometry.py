import copy
import json
import unittest
from pathlib import Path

import numpy as np

from modules.calibration_calculation.algorithm.geometry import fit_spot_transform
from modules.calibration_calculation.algorithm.types import CoordinateSystemError


ROOT = Path(__file__).resolve().parents[3]
CALIBRATION_PATH = ROOT / "data/mock/m2_image_recognition/spots_calib_ok.json"


def transformed_measurement(calibration: dict, transform: np.ndarray, translation=(0.0, 0.0)) -> dict:
    measurement = copy.deepcopy(calibration)
    measurement["image_type"] = "measurement"
    by_role = {spot["role"]: spot for spot in calibration["spots"]}
    center = np.array([by_role["center"]["x"], by_role["center"]["y"]], dtype=float)
    x_hint = np.array([by_role["x_positive"]["x"], by_role["x_positive"]["y"]]) - center
    y_hint = np.array([by_role["y_positive"]["x"], by_role["y_positive"]["y"]]) - center
    ex = x_hint / np.linalg.norm(x_hint)
    ey = y_hint - np.dot(y_hint, ex) * ex
    ey /= np.linalg.norm(ey)
    basis = np.column_stack([ex, ey])
    new_center = center + np.asarray(translation)
    for spot in measurement["spots"]:
        original = next(item for item in calibration["spots"] if item["spot_id"] == spot["spot_id"])
        vector = np.array([original["x"], original["y"]]) - center
        calibrated = basis.T @ vector
        point = new_center + basis @ (transform @ calibrated)
        spot["x"], spot["y"] = map(float, point)
    return measurement


class GeometryTests(unittest.TestCase):
    def setUp(self) -> None:
        self.calibration = json.loads(CALIBRATION_PATH.read_text(encoding="utf-8"))

    def test_identity_transform_is_recovered(self) -> None:
        measurement = transformed_measurement(self.calibration, np.eye(2))
        fit = fit_spot_transform(self.calibration, measurement)
        np.testing.assert_allclose(np.eye(2), fit.transform, atol=1e-12)
        self.assertLess(fit.rmse_pixel, 1e-10)

    def test_common_translation_does_not_change_transform(self) -> None:
        expected = np.array([[0.9, 0.04], [0.02, 1.1]])
        measurement = transformed_measurement(self.calibration, expected, translation=(17.0, -8.0))
        fit = fit_spot_transform(self.calibration, measurement)
        np.testing.assert_allclose(expected, fit.transform, atol=1e-12)

    def test_relative_axis_shifts_remove_center_translation(self) -> None:
        measurement = transformed_measurement(self.calibration, np.eye(2), translation=(10.0, 20.0))
        fit = fit_spot_transform(self.calibration, measurement)
        np.testing.assert_allclose((0.0, 0.0), fit.shifts["x_positive"], atol=1e-12)
        np.testing.assert_allclose((0.0, 0.0), fit.shifts["y_positive"], atol=1e-12)

    def test_mismatched_spot_ids_are_rejected(self) -> None:
        measurement = transformed_measurement(self.calibration, np.eye(2))
        measurement["spots"][1]["spot_id"] = 99
        with self.assertRaises(CoordinateSystemError):
            fit_spot_transform(self.calibration, measurement)

    def test_changed_role_is_rejected(self) -> None:
        measurement = transformed_measurement(self.calibration, np.eye(2))
        measurement["spots"][1]["role"] = "other"
        with self.assertRaises(CoordinateSystemError):
            fit_spot_transform(self.calibration, measurement)

    def test_collinear_outer_spots_are_rejected(self) -> None:
        calibration = copy.deepcopy(self.calibration)
        center = next(spot for spot in calibration["spots"] if spot["role"] == "center")
        outer_spots = [spot for spot in calibration["spots"] if spot["role"] != "center"]
        for index, spot in enumerate(outer_spots, start=1):
            spot["x"] = center["x"] + 20.0 * index
            spot["y"] = center["y"]
        measurement = copy.deepcopy(calibration)
        measurement["image_type"] = "measurement"
        with self.assertRaises(CoordinateSystemError):
            fit_spot_transform(calibration, measurement)


if __name__ == "__main__":
    unittest.main()
