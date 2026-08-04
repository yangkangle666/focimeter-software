import unittest

import numpy as np

from modules.calibration_calculation.algorithm.power_vector import (
    matrix_to_power_vector,
    power_matrix_from_transform,
    power_vector_to_matrix,
    power_vector_to_prescription,
    prescription_to_power_vector,
    transpose_to_minus_cylinder,
)
from modules.calibration_calculation.algorithm.types import PowerVector, Prescription
from modules.calibration_calculation.algorithm.types import CalculationError


def axis_error(left: float, right: float) -> float:
    difference = abs(left - right) % 180.0
    return min(difference, 180.0 - difference)


class PowerVectorTests(unittest.TestCase):
    def test_minus_cylinder_round_trip(self) -> None:
        for source in (
            Prescription(-2.0, -1.5, 0.0),
            Prescription(-2.0, -1.5, 45.0),
            Prescription(1.0, -0.75, 90.0),
            Prescription(0.0, -1.5, 179.0),
        ):
            recovered = power_vector_to_prescription(prescription_to_power_vector(source), 1e-12)
            self.assertAlmostEqual(source.S, recovered.S, places=12)
            self.assertAlmostEqual(source.C, recovered.C, places=12)
            self.assertLess(axis_error(source.A, recovered.A), 1e-10)

    def test_spherical_axis_is_null(self) -> None:
        result = power_vector_to_prescription(PowerVector(2.5, 0.0, 0.0), 0.05)
        self.assertEqual(Prescription(2.5, 0.0, None), result)

    def test_positive_cylinder_transposes_to_minus(self) -> None:
        result = transpose_to_minus_cylinder(Prescription(0.0, 1.5, 20.0))
        self.assertEqual(Prescription(1.5, -1.5, 110.0), result)

    def test_positive_cylinder_must_be_transposed_before_vector_conversion(self) -> None:
        with self.assertRaises(CalculationError):
            prescription_to_power_vector(Prescription(0.0, 1.5, 20.0))

    def test_matrix_and_vector_round_trip(self) -> None:
        vector = PowerVector(-2.75, 0.3, -0.6)
        recovered = matrix_to_power_vector(power_vector_to_matrix(vector))
        np.testing.assert_allclose(vector.as_array(), recovered.as_array(), atol=1e-15)

    def test_transform_recovers_power_matrix_and_skew(self) -> None:
        expected = np.array([[-2.0, 0.4], [0.4, -3.0]])
        transform = np.eye(2) - 0.03 * expected
        symmetric, skew = power_matrix_from_transform(transform, 0.03)
        np.testing.assert_allclose(expected, symmetric, atol=1e-14)
        self.assertLess(skew, 1e-14)


if __name__ == "__main__":
    unittest.main()
