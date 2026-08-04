#include "focimeter/m2/spot_matcher.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>
#include <utility>

namespace focimeter::m2 {
namespace {

constexpr double kPi = 3.14159265358979323846;

double radiansToDegrees(const double radians) {
    return radians * 180.0 / kPi;
}

double angularDifference(double left, double right) {
    double difference = std::fmod(left - right, 2.0 * kPi);
    if (difference > kPi) {
        difference -= 2.0 * kPi;
    } else if (difference < -kPi) {
        difference += 2.0 * kPi;
    }
    return std::abs(difference);
}

ErrorInfo makePairingError(std::string message) {
    ErrorInfo error;
    error.code = "COORDINATE_SYSTEM_INVALID";
    error.message = std::move(message);
    error.recoverable = true;
    return error;
}

struct AffineFit {
    double scale{1.0};
    double angle{0.0};
    cv::Point2d translation;
    double residual{std::numeric_limits<double>::infinity()};
    double maximum_residual{0.0};
    double center_residual{std::numeric_limits<double>::infinity()};
    double minimum_axis_scale{0.0};
    double maximum_axis_scale{0.0};
    double determinant{0.0};
    cv::Matx22d linear{cv::Matx22d::eye()};
    std::vector<int> permutation;
};

AffineFit fitAffine(
    const std::vector<cv::Point2d>& reference,
    const std::vector<cv::Point2d>& measured,
    const std::vector<int>& permutation) {
    AffineFit fit;
    fit.permutation = permutation;
    cv::Mat design(static_cast<int>(reference.size()), 3, CV_64F);
    cv::Mat target(static_cast<int>(reference.size()), 2, CV_64F);
    for (std::size_t index = 0; index < reference.size(); ++index) {
        design.at<double>(static_cast<int>(index), 0) = reference[index].x;
        design.at<double>(static_cast<int>(index), 1) = reference[index].y;
        design.at<double>(static_cast<int>(index), 2) = 1.0;
        const auto& destination = measured[static_cast<std::size_t>(permutation[index])];
        target.at<double>(static_cast<int>(index), 0) = destination.x;
        target.at<double>(static_cast<int>(index), 1) = destination.y;
    }
    cv::Mat parameters;
    if (!cv::solve(design, target, parameters, cv::DECOMP_SVD)) {
        return fit;
    }

    fit.linear = cv::Matx22d(
        parameters.at<double>(0, 0), parameters.at<double>(1, 0),
        parameters.at<double>(0, 1), parameters.at<double>(1, 1));
    fit.translation = cv::Point2d(parameters.at<double>(2, 0), parameters.at<double>(2, 1));
    fit.determinant = cv::determinant(fit.linear);
    if (!std::isfinite(fit.determinant)) {
        return fit;
    }

    const double a = fit.linear(0, 0);
    const double b = fit.linear(0, 1);
    const double c = fit.linear(1, 0);
    const double d = fit.linear(1, 1);
    const double trace = a * a + b * b + c * c + d * d;
    const double discriminant = std::sqrt(std::max(0.0, trace * trace - 4.0 * fit.determinant * fit.determinant));
    fit.maximum_axis_scale = std::sqrt(std::max(0.0, 0.5 * (trace + discriminant)));
    fit.minimum_axis_scale = std::sqrt(std::max(0.0, 0.5 * (trace - discriminant)));
    fit.scale = std::sqrt(std::abs(fit.determinant));
    fit.angle = std::atan2(c - b, a + d);

    double squared_error = 0.0;
    for (std::size_t index = 0; index < reference.size(); ++index) {
        const auto& point = reference[index];
        const cv::Point2d predicted = fit.linear * point + fit.translation;
        const cv::Point2d difference =
            predicted - measured[static_cast<std::size_t>(permutation[index])];
        const double point_residual = cv::norm(difference);
        squared_error += point_residual * point_residual;
        fit.maximum_residual = std::max(fit.maximum_residual, point_residual);
        if (index == 0) {
            fit.center_residual = point_residual;
        }
    }
    fit.residual = std::sqrt(squared_error / static_cast<double>(reference.size()));
    return fit;
}

}  // namespace

bool SpotMatcher::assignCalibrationRoles(
    const std::vector<SpotObservation>& observations,
    std::vector<Spot>& spots,
    ErrorInfo& error) const {
    spots.clear();
    error = {};
    if (observations.size() != 5) {
        error = makePairingError("Calibration role assignment requires exactly five spots.");
        error.number_details["detected_count"] = static_cast<double>(observations.size());
        return false;
    }

    std::size_t center_index = 0;
    std::vector<std::pair<double, std::size_t>> center_scores;
    for (std::size_t candidate = 0; candidate < observations.size(); ++candidate) {
        double score = 0.0;
        for (std::size_t other = 0; other < observations.size(); ++other) {
            if (candidate == other) {
                continue;
            }
            const cv::Point2d difference =
                observations[candidate].center - observations[other].center;
            if (!std::isfinite(difference.x) || !std::isfinite(difference.y) ||
                !std::isfinite(observations[candidate].confidence)) {
                error = makePairingError("Calibration observations must contain finite coordinates and confidence.");
                return false;
            }
            score += difference.dot(difference);
        }
        center_scores.emplace_back(score, candidate);
    }
    std::sort(center_scores.begin(), center_scores.end());
    if (center_scores[0].first <= 1e-9 ||
        center_scores[1].first <= center_scores[0].first * 1.15) {
        error = makePairingError("Calibration center identity is ambiguous.");
        error.number_details["best_center_score"] = center_scores[0].first;
        error.number_details["second_center_score"] = center_scores[1].first;
        return false;
    }
    center_index = center_scores[0].second;

    std::vector<std::size_t> outer_indices;
    std::vector<double> radii;
    for (std::size_t index = 0; index < observations.size(); ++index) {
        if (index == center_index) {
            continue;
        }
        outer_indices.push_back(index);
        radii.push_back(cv::norm(observations[index].center - observations[center_index].center));
    }
    const auto [minimum_radius, maximum_radius] = std::minmax_element(radii.begin(), radii.end());
    if (*minimum_radius <= 1e-6 || *maximum_radius / *minimum_radius > 2.5) {
        error = makePairingError("Calibration spot geometry is not a stable center-and-four-directions pattern.");
        error.number_details["minimum_radius"] = *minimum_radius;
        error.number_details["maximum_radius"] = *maximum_radius;
        return false;
    }

    const std::array<double, 4> target_angles{-kPi / 2.0, kPi, kPi / 2.0, 0.0};
    const std::array<int, 4> target_ids{1, 2, 3, 4};
    const std::array<const char*, 4> target_roles{
        "y_positive", "left_or_negative", "other", "x_positive"};

    std::array<int, 4> permutation{0, 1, 2, 3};
    std::array<int, 4> best_permutation{};
    double best_cost = std::numeric_limits<double>::infinity();
    double second_cost = std::numeric_limits<double>::infinity();
    double best_max_error = std::numeric_limits<double>::infinity();
    do {
        double cost = 0.0;
        double maximum_error = 0.0;
        for (std::size_t target = 0; target < target_angles.size(); ++target) {
            const auto observation_index =
                outer_indices[static_cast<std::size_t>(permutation[target])];
            const cv::Point2d direction =
                observations[observation_index].center - observations[center_index].center;
            const double angle = std::atan2(direction.y, direction.x);
            const double difference = angularDifference(angle, target_angles[target]);
            cost += difference * difference;
            maximum_error = std::max(maximum_error, difference);
        }

        if (cost < best_cost) {
            second_cost = best_cost;
            best_cost = cost;
            best_max_error = maximum_error;
            best_permutation = permutation;
        } else if (cost < second_cost) {
            second_cost = cost;
        }
    } while (std::next_permutation(permutation.begin(), permutation.end()));

    // Keep calibration axes away from the 45-degree role-switch boundary.
    const double maximum_role_error = 35.0 * kPi / 180.0;
    const double minimum_cost_margin = std::pow(10.0 * kPi / 180.0, 2.0);
    if (best_max_error > maximum_role_error || second_cost - best_cost < minimum_cost_margin) {
        error = makePairingError("Calibration spot roles are ambiguous in image coordinates.");
        error.number_details["maximum_angle_error_degrees"] = radiansToDegrees(best_max_error);
        return false;
    }

    Spot center;
    center.spot_id = 0;
    center.role = "center";
    center.center = observations[center_index].center;
    center.confidence = observations[center_index].confidence;
    spots.push_back(center);

    for (std::size_t target = 0; target < target_angles.size(); ++target) {
        const auto observation_index =
            outer_indices[static_cast<std::size_t>(best_permutation[target])];
        Spot spot;
        spot.spot_id = target_ids[target];
        spot.role = target_roles[target];
        spot.center = observations[observation_index].center;
        spot.confidence = observations[observation_index].confidence;
        spots.push_back(spot);
    }
    std::sort(spots.begin(), spots.end(), [](const Spot& left, const Spot& right) {
        return left.spot_id < right.spot_id;
    });
    return true;
}

bool SpotMatcher::matchMeasurement(
    const std::vector<Spot>& calibration_spots,
    const std::vector<SpotObservation>& measurement_observations,
    const ProcessingConfig& config,
    std::vector<Spot>& measurement_spots,
    MatchDiagnostics& diagnostics,
    ErrorInfo& error) const {
    measurement_spots.clear();
    diagnostics = {};
    error = {};
    if (calibration_spots.size() != 5 || measurement_observations.size() != 5) {
        error = makePairingError("Cross-image matching requires five calibration and five measurement spots.");
        error.number_details["calibration_count"] =
            static_cast<double>(calibration_spots.size());
        error.number_details["measurement_count"] =
            static_cast<double>(measurement_observations.size());
        return false;
    }
    if (!std::isfinite(config.max_rotation_degrees) ||
        !std::isfinite(config.min_scale) || !std::isfinite(config.max_scale) ||
        !std::isfinite(config.max_residual_ratio) || !std::isfinite(config.ambiguity_ratio) ||
        !std::isfinite(config.min_confidence) ||
        config.max_rotation_degrees <= 0.0 || config.max_rotation_degrees >= 45.0 ||
        config.min_scale <= 0.0 || config.min_scale > 1.0 ||
        config.max_scale < 1.0 || config.max_scale < config.min_scale ||
        config.max_residual_ratio <= 0.0 || config.max_residual_ratio > 1.0 ||
        config.ambiguity_ratio <= 1.0 ||
        config.min_confidence < 0.0 || config.min_confidence > 1.0) {
        error = makePairingError("Cross-image matching configuration is invalid.");
        return false;
    }

    std::vector<Spot> ordered_calibration = calibration_spots;
    std::sort(
        ordered_calibration.begin(),
        ordered_calibration.end(),
        [](const Spot& left, const Spot& right) { return left.spot_id < right.spot_id; });
    const std::array<const char*, 5> expected_roles{
        "center", "y_positive", "left_or_negative", "other", "x_positive"};
    for (std::size_t index = 0; index < ordered_calibration.size(); ++index) {
        const auto& spot = ordered_calibration[index];
        if (spot.spot_id != static_cast<int>(index) ||
            spot.role != expected_roles[index] ||
            !std::isfinite(spot.center.x) || !std::isfinite(spot.center.y) ||
            !std::isfinite(spot.confidence) || spot.confidence < 0.0 || spot.confidence > 1.0) {
            error = makePairingError(
                "Calibration spots must contain IDs 0 through 4 with valid roles, coordinates, and confidence.");
            return false;
        }
    }

    std::vector<cv::Point2d> reference;
    std::vector<cv::Point2d> measured;
    for (const auto& spot : ordered_calibration) {
        reference.push_back(spot.center);
    }
    for (const auto& observation : measurement_observations) {
        if (!std::isfinite(observation.center.x) || !std::isfinite(observation.center.y) ||
            !std::isfinite(observation.confidence) ||
            observation.confidence < 0.0 || observation.confidence > 1.0) {
            error = makePairingError("Measurement observations must contain finite coordinates and confidence.");
            return false;
        }
        measured.push_back(observation.center);
    }

    cv::Point2d center;
    for (const auto& point : reference) {
        center += point;
    }
    center *= 1.0 / static_cast<double>(reference.size());
    double mean_radius = 0.0;
    for (const auto& point : reference) {
        mean_radius += cv::norm(point - center);
    }
    mean_radius /= static_cast<double>(reference.size());
    const double residual_limit = std::max(1.5, mean_radius * config.max_residual_ratio);

    std::vector<AffineFit> candidates;
    std::vector<int> permutation(measured.size());
    std::iota(permutation.begin(), permutation.end(), 0);
    do {
        AffineFit fit = fitAffine(reference, measured, permutation);
        const double angle_degrees = std::abs(radiansToDegrees(fit.angle));
        if (fit.determinant <= 0.0 ||
            fit.minimum_axis_scale < config.min_scale ||
            fit.maximum_axis_scale > config.max_scale ||
            angle_degrees > config.max_rotation_degrees) {
            continue;
        }
        candidates.push_back(std::move(fit));
    } while (std::next_permutation(permutation.begin(), permutation.end()));

    if (candidates.empty()) {
        error = makePairingError("No one-to-one spot pairing satisfies the scale and rotation limits.");
        error.number_details["max_rotation_degrees"] = config.max_rotation_degrees;
        return false;
    }
    std::sort(candidates.begin(), candidates.end(), [](const AffineFit& left, const AffineFit& right) {
        return left.residual < right.residual;
    });

    const AffineFit& best = candidates.front();
    const double center_residual_limit = std::max(1.0, residual_limit * 0.5);
    if (best.residual > residual_limit || best.maximum_residual > residual_limit ||
        best.center_residual > center_residual_limit) {
        error = makePairingError("Best cross-image pairing exceeds the global, point, or center residual limit.");
        error.number_details["residual_pixels"] = best.residual;
        error.number_details["maximum_residual_pixels"] = best.maximum_residual;
        error.number_details["center_residual_pixels"] = best.center_residual;
        error.number_details["residual_limit_pixels"] = residual_limit;
        error.number_details["center_residual_limit_pixels"] = center_residual_limit;
        return false;
    }

    if (candidates.size() > 1) {
        const double ambiguity_limit = std::max(
            best.residual * config.ambiguity_ratio,
            best.residual + 0.25);
        if (candidates[1].residual <= ambiguity_limit) {
            error = makePairingError("Multiple cross-image spot pairings are equally plausible.");
            error.number_details["best_residual_pixels"] = best.residual;
            error.number_details["second_residual_pixels"] = candidates[1].residual;
            return false;
        }
    }

    diagnostics.scale = best.scale;
    diagnostics.rotation_degrees = radiansToDegrees(best.angle);
    diagnostics.residual_pixels = best.residual;
    diagnostics.residual_limit_pixels = residual_limit;
    const double match_confidence = std::clamp(
        1.0 - 0.20 * best.residual / residual_limit -
            0.10 * best.maximum_residual / residual_limit,
        0.70,
        1.0);

    for (std::size_t index = 0; index < ordered_calibration.size(); ++index) {
        const auto observation_index = static_cast<std::size_t>(best.permutation[index]);
        const double confidence = std::min(
            measurement_observations[observation_index].confidence,
            match_confidence);
        if (confidence < config.min_confidence) {
            measurement_spots.clear();
            error = makePairingError("Cross-image pairing confidence is below the configured threshold.");
            error.number_details["min_confidence"] = config.min_confidence;
            error.number_details["pairing_confidence"] = confidence;
            return false;
        }
        Spot spot;
        spot.spot_id = ordered_calibration[index].spot_id;
        spot.role = ordered_calibration[index].role;
        spot.center = measurement_observations[observation_index].center;
        spot.confidence = confidence;
        measurement_spots.push_back(std::move(spot));
    }
    return true;
}

}  // namespace focimeter::m2
