import random
import unittest

import numpy as np

from modules.calibration_calculation.algorithm.experimental_input import (
    ExperimentalObservation,
    ExperimentalPair,
)
from modules.calibration_calculation.algorithm.multispot_matching import match_experimental_multispot
from modules.calibration_calculation.algorithm.types import CoordinateSystemError, MatchingLimits


def lattice_pair(
    measurement_count=39,
    randomize=False,
    transform=None,
    translation=(5.0, -3.0),
    symmetric=False,
    installation_degree=0.0,
):
    if symmetric:
        coordinates = [(x, y) for y in range(-3, 4) for x in range(-3, 4) if x * x + y * y <= 11]
    else:
        removed = {(-3, -3), (-2, -3), (3, -2), (-3, 2), (2, 3), (3, 3)}
        coordinates = [(x, y) for y in range(-3, 4) for x in range(-3, 4) if (x, y) not in removed]
    measurement_coordinates = sorted(coordinates, key=lambda item: (item[0] * item[0] + item[1] * item[1], item[1], item[0]))[:measurement_count]
    matrix = np.asarray(transform if transform is not None else [[0.98, 0.015], [-0.01, 1.02]])
    offset = np.asarray(translation)
    installation_radians = np.deg2rad(installation_degree)
    installation = np.asarray([
        [np.cos(installation_radians), -np.sin(installation_radians)],
        [np.sin(installation_radians), np.cos(installation_radians)],
    ])
    center = np.asarray([640.0, 512.0])
    calibration = [
        ExperimentalObservation(
            1000 + index * 7,
            *(center + installation @ np.asarray([40.0 * x, 40.0 * y])),
            0.95,
        )
        for index, (x, y) in enumerate(coordinates)
    ]
    measurement = []
    for index, (x, y) in enumerate(measurement_coordinates):
        source = center + installation @ np.asarray([40.0 * x, 40.0 * y])
        target = matrix @ source + offset
        measurement.append(ExperimentalObservation(17 + index * 11, *target, 0.93))
    if randomize:
        random.Random(20260804).shuffle(calibration)
        random.Random(42).shuffle(measurement)
    return ExperimentalPair("synthetic_multispot", tuple(calibration), tuple(measurement))


def shifted_pair(pair, offset):
    dx, dy = offset
    return ExperimentalPair(
        pair.task_id,
        tuple(
            ExperimentalObservation(item.detection_id, item.x + dx, item.y + dy, item.confidence)
            for item in pair.calibration
        ),
        tuple(
            ExperimentalObservation(item.detection_id, item.x + dx, item.y + dy, item.confidence)
            for item in pair.measurement
        ),
    )


def centered_rotation(degrees, center=(640.0, 512.0), displacement=(2.0, -1.0)):
    radians = np.deg2rad(degrees)
    matrix = np.asarray([
        [np.cos(radians), -np.sin(radians)],
        [np.sin(radians), np.cos(radians)],
    ])
    center_vector = np.asarray(center)
    translation = center_vector + np.asarray(displacement) - matrix @ center_vector
    return matrix, translation


class MultispotMatchingTests(unittest.TestCase):
    def setUp(self):
        self.limits = MatchingLimits.simulation_defaults()

    def test_partial_overlap_and_order_change_match(self):
        pair = lattice_pair(measurement_count=27, randomize=True)
        matched = match_experimental_multispot(pair, self.limits)
        self.assertEqual(43, matched.diagnostics.calibration_detection_count)
        self.assertEqual(27, matched.diagnostics.measurement_detection_count)
        self.assertEqual(len(pair.measurement), matched.diagnostics.matched_spot_count)
        self.assertAlmostEqual(27 / 43, matched.diagnostics.overlap_ratio)
        self.assertGreater(len(pair.calibration), matched.diagnostics.matched_spot_count)
        self.assertEqual(
            set(range(matched.diagnostics.matched_spot_count)),
            {spot["spot_id"] for spot in matched.calibration["spots"]},
        )

    def test_partial_overlap_43_to_39_matches(self):
        matched = match_experimental_multispot(lattice_pair(measurement_count=39), self.limits)
        self.assertEqual(43, matched.diagnostics.calibration_detection_count)
        self.assertEqual(39, matched.diagnostics.measurement_detection_count)
        self.assertEqual(39, matched.diagnostics.matched_spot_count)
        self.assertAlmostEqual(39 / 43, matched.diagnostics.overlap_ratio)

    def test_reference_overlap_43_to_25_is_rejected(self):
        with self.assertRaises(CoordinateSystemError):
            match_experimental_multispot(lattice_pair(measurement_count=25), self.limits)

    def test_reference_overlap_43_to_26_passes_at_configured_boundary(self):
        matched = match_experimental_multispot(lattice_pair(measurement_count=26), self.limits)
        self.assertEqual(26, matched.diagnostics.matched_spot_count)
        self.assertAlmostEqual(26 / 43, matched.diagnostics.overlap_ratio)
        self.assertGreaterEqual(matched.diagnostics.overlap_ratio, self.limits.min_overlap_ratio)

    def test_detection_ids_are_not_copied_to_spot_ids(self):
        matched = match_experimental_multispot(lattice_pair(randomize=True), self.limits)
        spot_ids = {spot["spot_id"] for spot in matched.calibration["spots"]}
        detection_ids = {item[1] for item in matched.detection_pairs}
        self.assertTrue(spot_ids.isdisjoint(detection_ids))

    def test_independent_detection_id_changes_do_not_change_coordinate_pairs(self):
        original = match_experimental_multispot(lattice_pair(), self.limits)
        pair = lattice_pair()
        replaced = ExperimentalPair(
            pair.task_id,
            tuple(ExperimentalObservation(index, item.x, item.y, item.confidence) for index, item in enumerate(pair.calibration)),
            tuple(ExperimentalObservation(9000 + index, item.x, item.y, item.confidence) for index, item in enumerate(pair.measurement)),
        )
        changed = match_experimental_multispot(replaced, self.limits)
        original_coordinates = [(item["x"], item["y"]) for item in original.measurement["spots"]]
        changed_coordinates = [(item["x"], item["y"]) for item in changed.measurement["spots"]]
        self.assertEqual(original_coordinates, changed_coordinates)

    def test_low_confidence_is_rejected(self):
        pair = lattice_pair()
        measurement = list(pair.measurement)
        item = measurement[0]
        measurement[0] = ExperimentalObservation(item.detection_id, item.x, item.y, 0.1)
        with self.assertRaises(CoordinateSystemError):
            match_experimental_multispot(ExperimentalPair(pair.task_id, pair.calibration, tuple(measurement)), self.limits)

    def test_off_lattice_extra_detection_is_rejected(self):
        pair = lattice_pair()
        measurement = pair.measurement + (ExperimentalObservation(99999, 333.3, 777.7, 0.95),)
        with self.assertRaisesRegex(CoordinateSystemError, "39 of 40"):
            match_experimental_multispot(
                ExperimentalPair(pair.task_id, pair.calibration, measurement),
                self.limits,
            )

    def test_insufficient_overlap_is_rejected(self):
        pair = lattice_pair(measurement_count=10)
        with self.assertRaises(CoordinateSystemError):
            match_experimental_multispot(pair, self.limits)

    def test_ninety_degree_rotation_is_rejected(self):
        with self.assertRaises(CoordinateSystemError):
            match_experimental_multispot(
                lattice_pair(measurement_count=37, transform=[[0.0, -1.0], [1.0, 0.0]], translation=(1152.0, -128.0), symmetric=True),
                self.limits,
            )

    def test_integer_pitch_translation_is_rejected(self):
        with self.assertRaises(CoordinateSystemError):
            match_experimental_multispot(lattice_pair(transform=np.eye(2), translation=(40.0, 0.0)), self.limits)

    def test_five_and_ten_degree_rotation_about_image_center_are_accepted(self):
        for degrees in (5.0, 10.0):
            with self.subTest(degrees=degrees):
                matrix, translation = centered_rotation(degrees)
                matched = match_experimental_multispot(
                    lattice_pair(transform=matrix, translation=translation),
                    self.limits,
                )
                self.assertEqual(39, matched.diagnostics.matched_spot_count)

    def test_common_42_degree_installation_with_small_relative_rotation_is_accepted(self):
        for degrees in (-5.0, 5.0, 10.0):
            with self.subTest(degrees=degrees):
                matrix, translation = centered_rotation(degrees)
                matched = match_experimental_multispot(
                    lattice_pair(
                        transform=matrix,
                        translation=translation,
                        installation_degree=42.0,
                    ),
                    self.limits,
                )
                self.assertEqual(39, matched.diagnostics.matched_spot_count)

    def test_exact_45_degree_installation_with_small_relative_rotation_is_accepted(self):
        for degrees in (-10.0, -5.0, 0.0, 5.0, 10.0):
            with self.subTest(degrees=degrees):
                matrix, translation = centered_rotation(degrees)
                matched = match_experimental_multispot(
                    lattice_pair(
                        transform=matrix,
                        translation=translation,
                        installation_degree=45.0,
                    ),
                    self.limits,
                )
                self.assertEqual(39, matched.diagnostics.matched_spot_count)

    def test_relative_rotation_over_limit_is_rejected_at_42_degree_installation(self):
        matrix, translation = centered_rotation(16.0)
        with self.assertRaises(CoordinateSystemError):
            match_experimental_multispot(
                lattice_pair(
                    transform=matrix,
                    translation=translation,
                    installation_degree=42.0,
                ),
                self.limits,
            )

    def test_common_coordinate_origin_shift_does_not_change_matching(self):
        matrix, translation = centered_rotation(10.0)
        pair = lattice_pair(transform=matrix, translation=translation)
        original = match_experimental_multispot(pair, self.limits)
        shifted = match_experimental_multispot(shifted_pair(pair, (2500.0, -1700.0)), self.limits)
        self.assertEqual(original.detection_pairs, shifted.detection_pairs)
        self.assertAlmostEqual(original.diagnostics.overlap_ratio, shifted.diagnostics.overlap_ratio)
        self.assertAlmostEqual(
            original.diagnostics.matching_rmse_pixel,
            shifted.diagnostics.matching_rmse_pixel,
            places=8,
        )

    def test_reflection_is_rejected(self):
        with self.assertRaises(CoordinateSystemError):
            match_experimental_multispot(
                lattice_pair(measurement_count=37, transform=[[-1.0, 0.0], [0.0, 1.0]], translation=(1280.0, 0.0), symmetric=True),
                self.limits,
            )


if __name__ == "__main__":
    unittest.main()
