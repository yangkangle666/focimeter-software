import unittest

import numpy as np

from modules.calibration_calculation.algorithm.calibration import (
    _validation_metrics,
    apply_correction,
    calibration_dataset_sha256,
    canonical_sha256,
    fit_linear_correction,
)
from modules.calibration_calculation.algorithm.types import (
    CalibrationDataError,
    CalibrationModel,
    PowerVector,
    Prescription,
)


class CalibrationAlgorithmTests(unittest.TestCase):
    def setUp(self) -> None:
        self.raw = np.array(
            [
                [-5.0, 0.0, 0.0],
                [5.0, 0.0, 0.0],
                [0.75, 0.75, 0.0],
                [0.75, -0.75, 0.0],
                [0.75, 0.0, 0.75],
                [0.75, 0.0, -0.75],
            ]
        )
        self.weights = np.ones(len(self.raw))

    def test_known_affine_correction_is_recovered(self) -> None:
        expected_w = np.array([[1.1, 0.1, 0.0], [0.0, 0.9, 0.2], [0.1, 0.0, 1.2]])
        expected_b = np.array([0.05, -0.02, 0.03])
        certified = self.raw @ expected_w.T + expected_b
        fitted_w, fitted_b = fit_linear_correction(self.raw, certified, self.weights)
        np.testing.assert_allclose(expected_w, fitted_w, atol=1e-12)
        np.testing.assert_allclose(expected_b, fitted_b, atol=1e-12)

    def test_rank_deficient_training_set_is_rejected(self) -> None:
        deficient = np.column_stack([self.raw[:, 0], self.raw[:, 0], self.raw[:, 0]])
        with self.assertRaises(CalibrationDataError):
            fit_linear_correction(deficient, self.raw, self.weights)

    def test_nonpositive_weights_are_rejected(self) -> None:
        weights = self.weights.copy()
        weights[0] = 0.0
        with self.assertRaises(CalibrationDataError):
            fit_linear_correction(self.raw, self.raw, weights)

    def test_apply_correction_uses_matrix_and_bias(self) -> None:
        model_dict = {
            "schema_version": "1.0",
            "model_type": "hybrid_power_matrix_v1",
            "model_id": "test",
            "validation_status": "simulation_only",
            "source_dataset_sha256": "a" * 64,
            "hardware": {"distance_m": 0.03, "expected_spot_count": 5},
            "correction": {"matrix": [[2, 0, 0], [0, 3, 0], [0, 0, 4]], "bias": [1, 2, 3]},
            "quality_limits": {
                "validated_sphere_range_D": [-5, 5],
                "validated_abs_cylinder_max_D": 1.5,
                "cylinder_threshold_D": 0.01,
                "max_fit_rmse_pixel": 1,
                "max_condition_number": 10,
                "max_skew_power_D": 1,
                "min_confidence": 0.7,
                "validation_confidence": 0.9
            },
            "fit_metrics": {},
            "standard_lenses": []
        }
        model = CalibrationModel.from_dict(model_dict)
        corrected = apply_correction(PowerVector(1, 2, 3), model)
        np.testing.assert_allclose([3, 8, 15], corrected.as_array())

    def test_canonical_hash_ignores_mapping_order(self) -> None:
        self.assertEqual(canonical_sha256({"a": 1, "b": 2}), canonical_sha256({"b": 2, "a": 1}))

    def test_dataset_hash_includes_loaded_spot_documents(self) -> None:
        dataset = {"dataset_id": "test", "samples": [{"sample_id": "sample"}]}
        original = [{"sample_id": "sample", "spots_calib": {"x": 1}, "spots_meas": {"x": 2}}]
        changed = [{"sample_id": "sample", "spots_calib": {"x": 1}, "spots_meas": {"x": 3}}]
        self.assertNotEqual(
            calibration_dataset_sha256(dataset, original),
            calibration_dataset_sha256(dataset, changed),
        )

    def test_validation_rejects_sphere_error_on_cylindrical_samples(self) -> None:
        expected = [
            Prescription(0.0, -1.0, 90.0),
            Prescription(0.0, -1.0, 90.0),
            Prescription(0.0, 0.0, None),
            Prescription(0.0, 0.0, None),
            Prescription(1.0, 0.0, None),
        ]
        predicted = [
            Prescription(10.0, -1.0, 90.0),
            Prescription(10.0, -1.0, 90.0),
            *expected[2:],
        ]
        samples = [
            {"serial_number": "cylinder"},
            {"serial_number": "cylinder"},
            {"serial_number": "zero"},
            {"serial_number": "zero"},
            {"serial_number": "sphere"},
        ]
        metrics, passed = _validation_metrics(predicted, expected, samples)
        self.assertFalse(passed)
        self.assertEqual(10.0, metrics["max_sphere_error_D"])

    def test_validation_rejects_cylinder_error_on_spherical_samples(self) -> None:
        expected = [
            Prescription(0.0, -1.0, 90.0),
            Prescription(0.0, -1.0, 90.0),
            Prescription(0.0, 0.0, None),
            Prescription(0.0, 0.0, None),
            Prescription(1.0, 0.0, None),
        ]
        predicted = [*expected[:-1], Prescription(1.0, -0.5, 0.0)]
        samples = [
            {"serial_number": "cylinder"},
            {"serial_number": "cylinder"},
            {"serial_number": "zero"},
            {"serial_number": "zero"},
            {"serial_number": "sphere"},
        ]
        metrics, passed = _validation_metrics(predicted, expected, samples)
        self.assertFalse(passed)
        self.assertEqual(0.5, metrics["max_cylinder_error_D"])


if __name__ == "__main__":
    unittest.main()
