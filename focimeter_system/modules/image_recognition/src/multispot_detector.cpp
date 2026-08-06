#include "focimeter/m2/multispot_detector.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <numeric>
#include <string>
#include <vector>

#include <opencv2/imgproc.hpp>

namespace focimeter::m2 {
namespace {

ErrorInfo makeError(std::string code, std::string message, const bool recoverable) {
    ErrorInfo error;
    error.code = std::move(code);
    error.message = std::move(message);
    error.recoverable = recoverable;
    return error;
}

void addWarning(ImageDiagnostics& diagnostics, const std::string& warning) {
    if (std::find(diagnostics.warnings.begin(), diagnostics.warnings.end(), warning) ==
        diagnostics.warnings.end()) {
        diagnostics.warnings.push_back(warning);
    }
}

void addFlag(SpotObservation& observation, const std::string& flag) {
    if (std::find(observation.quality_flags.begin(), observation.quality_flags.end(), flag) ==
        observation.quality_flags.end()) {
        observation.quality_flags.push_back(flag);
    }
}

double clamp01(const double value) {
    return std::clamp(value, 0.0, 1.0);
}

bool hasWarning(const ImageDiagnostics& diagnostics, const std::string& warning) {
    return std::find(diagnostics.warnings.begin(), diagnostics.warnings.end(), warning) !=
        diagnostics.warnings.end();
}

int makeOddKernel(const int configured_size) {
    const int minimum = std::max(3, configured_size);
    return minimum % 2 == 0 ? minimum + 1 : minimum;
}

double borderMean(const cv::Mat& image) {
    const int border_width = std::max(1, std::min({8, image.rows / 2, image.cols / 2}));
    cv::Mat mask = cv::Mat::zeros(image.size(), CV_8UC1);
    mask.rowRange(0, border_width).setTo(255);
    mask.rowRange(image.rows - border_width, image.rows).setTo(255);
    mask.colRange(0, border_width).setTo(255);
    mask.colRange(image.cols - border_width, image.cols).setTo(255);
    return cv::mean(image, mask)[0];
}

double percentile(const cv::Mat& image, const double fraction) {
    CV_Assert(image.type() == CV_8UC1);
    const int channels[] = {0};
    const int histogram_size[] = {256};
    const float range[] = {0.0F, 256.0F};
    const float* ranges[] = {range};
    cv::Mat histogram;
    cv::calcHist(&image, 1, channels, cv::Mat{}, histogram, 1, histogram_size, ranges, true, false);

    const double target = std::clamp(fraction, 0.0, 1.0) * static_cast<double>(image.total() - 1U);
    double cumulative = 0.0;
    for (int value = 0; value < 256; ++value) {
        cumulative += histogram.at<float>(value);
        if (cumulative > target) {
            return static_cast<double>(value);
        }
    }
    return 255.0;
}

double medianArea(const std::vector<SpotObservation>& observations) {
    std::vector<double> areas;
    areas.reserve(observations.size());
    for (const auto& observation : observations) {
        areas.push_back(observation.area);
    }
    std::sort(areas.begin(), areas.end());
    return areas[areas.size() / 2U];
}

double medianIntegratedIntensity(const std::vector<SpotObservation>& observations) {
    std::vector<double> values;
    values.reserve(observations.size());
    for (const auto& observation : observations) {
        values.push_back(observation.integrated_intensity);
    }
    std::sort(values.begin(), values.end());
    return values[values.size() / 2U];
}

double medianResidualPeak(const std::vector<SpotObservation>& observations) {
    std::vector<double> values;
    values.reserve(observations.size());
    for (const auto& observation : observations) {
        values.push_back(observation.peak_residual_intensity);
    }
    std::sort(values.begin(), values.end());
    return values[values.size() / 2U];
}

bool hasContinuousBrightLine(
    const cv::Mat& intensity,
    const cv::Point start,
    const cv::Point end,
    const double threshold) {
    if (start == end) {
        return true;
    }
    const cv::Rect image_bounds(0, 0, intensity.cols, intensity.rows);
    if (!image_bounds.contains(start) || !image_bounds.contains(end)) {
        return false;
    }
    cv::LineIterator iterator(intensity, start, end, 8);
    if (iterator.count <= 0) {
        return false;
    }
    for (int sample = 0; sample < iterator.count; ++sample, ++iterator) {
        if (**iterator < threshold) {
            return false;
        }
    }
    return true;
}

bool hasCommonBrightBridge(
    const SpotObservation& primary,
    const SpotObservation& candidate,
    const cv::Mat& intensity,
    const cv::Rect& roi_rect,
    const double background) {
    const cv::Point primary_point(
        cvRound(primary.center.x - roi_rect.x),
        cvRound(primary.center.y - roi_rect.y));
    const cv::Point candidate_point(
        cvRound(candidate.center.x - roi_rect.x),
        cvRound(candidate.center.y - roi_rect.y));
    constexpr double kMinimumPeakBridgeFraction = 0.50;
    const double lower_peak = std::min(primary.peak_intensity, candidate.peak_intensity);
    const double bridge_threshold = background +
        std::max(8.0, kMinimumPeakBridgeFraction * std::max(0.0, lower_peak - background));
    return hasContinuousBrightLine(
        intensity, primary_point, candidate_point, bridge_threshold);
}

double medianNearestNeighborDistance(const std::vector<SpotObservation>& observations) {
    if (observations.size() < 3U) {
        return std::numeric_limits<double>::infinity();
    }
    std::vector<double> nearest_distances;
    nearest_distances.reserve(observations.size());
    for (std::size_t left = 0; left < observations.size(); ++left) {
        double nearest = std::numeric_limits<double>::infinity();
        for (std::size_t right = 0; right < observations.size(); ++right) {
            if (left == right) {
                continue;
            }
            nearest = std::min(
                nearest,
                cv::norm(observations[left].center - observations[right].center));
        }
        nearest_distances.push_back(nearest);
    }
    std::sort(nearest_distances.begin(), nearest_distances.end());
    return nearest_distances[nearest_distances.size() / 2U];
}

std::vector<cv::Point2d> collectLatticeStepVectors(
    const std::vector<SpotObservation>& anchors,
    const double typical_spacing) {
    std::vector<cv::Point2d> steps;
    if (!std::isfinite(typical_spacing) || typical_spacing <= 0.0) {
        return steps;
    }
    constexpr double kMinimumStepRatio = 0.80;
    constexpr double kMaximumStepRatio = 1.25;
    for (std::size_t left = 0; left < anchors.size(); ++left) {
        for (std::size_t right = left + 1U; right < anchors.size(); ++right) {
            const cv::Point2d delta = anchors[right].center - anchors[left].center;
            const double distance = cv::norm(delta);
            if (distance >= kMinimumStepRatio * typical_spacing &&
                distance <= kMaximumStepRatio * typical_spacing) {
                steps.push_back(delta);
            }
        }
    }
    return steps;
}

bool matchesRepeatedLatticeStep(
    const cv::Point2d& delta,
    const std::vector<cv::Point2d>& lattice_steps,
    const double typical_spacing) {
    const double distance = cv::norm(delta);
    constexpr double kMinimumStepRatio = 0.80;
    constexpr double kMaximumStepRatio = 1.25;
    if (distance < kMinimumStepRatio * typical_spacing ||
        distance > kMaximumStepRatio * typical_spacing) {
        return false;
    }

    constexpr double kMinimumLengthRatio = 0.85;
    constexpr double kMaximumLengthRatio = 1.18;
    constexpr double kMinimumDirectionCosine = 0.97;
    constexpr int kMinimumRepeatedSteps = 2;
    int matching_steps = 0;
    for (const auto& step : lattice_steps) {
        const double step_length = cv::norm(step);
        if (step_length <= 0.0) {
            continue;
        }
        const double length_ratio = distance / step_length;
        const double direction_cosine =
            std::abs(delta.dot(step) / (distance * step_length));
        if (length_ratio >= kMinimumLengthRatio &&
            length_ratio <= kMaximumLengthRatio &&
            direction_cosine >= kMinimumDirectionCosine &&
            ++matching_steps >= kMinimumRepeatedSteps) {
            return true;
        }
    }
    return false;
}

enum class LatticeRecoveryDecision {
    Supported,
    SignalInsufficient,
    StepUnsupported,
    GeometryUnsupported,
};

LatticeRecoveryDecision evaluateLatticeSupport(
    const SpotObservation& candidate,
    const std::vector<SpotObservation>& anchors,
    const std::vector<cv::Point2d>& lattice_steps,
    const double typical_spacing,
    const double median_integrated_intensity,
    const double median_residual_peak) {
    if (anchors.size() < 3U || !std::isfinite(typical_spacing) || typical_spacing <= 0.0) {
        return LatticeRecoveryDecision::StepUnsupported;
    }

    // A dim or broad spot can leave a small top-hat support. Retain it only when
    // both its signal and its local geometry agree with the stable lattice.
    const bool signal_supported =
        candidate.integrated_intensity >= 0.08 * median_integrated_intensity &&
        candidate.peak_residual_intensity >= 0.35 * median_residual_peak;
    if (!signal_supported) {
        return LatticeRecoveryDecision::SignalInsufficient;
    }

    constexpr double kMaximumOpposingDot = -0.70;
    constexpr double kMaximumOrthogonalAbsoluteDot = 0.35;
    std::vector<cv::Point2d> directions;
    for (const auto& anchor : anchors) {
        const cv::Point2d delta = anchor.center - candidate.center;
        const double distance = cv::norm(delta);
        if (!matchesRepeatedLatticeStep(delta, lattice_steps, typical_spacing)) {
            continue;
        }
        directions.push_back(delta * (1.0 / distance));
    }

    for (std::size_t left = 0; left < directions.size(); ++left) {
        for (std::size_t right = left + 1U; right < directions.size(); ++right) {
            const double dot = directions[left].dot(directions[right]);
            const bool opposing_support = dot <= kMaximumOpposingDot;
            const bool orthogonal_support = std::abs(dot) <= kMaximumOrthogonalAbsoluteDot;
            if (opposing_support || orthogonal_support) {
                return LatticeRecoveryDecision::Supported;
            }
        }
    }
    return directions.size() < 2U
        ? LatticeRecoveryDecision::StepUnsupported
        : LatticeRecoveryDecision::GeometryUnsupported;
}

void recordRejectedRecoveryDecision(
    const LatticeRecoveryDecision decision,
    ImageDiagnostics& diagnostics) {
    ++diagnostics.lattice_recovery_considered_count;
    switch (decision) {
        case LatticeRecoveryDecision::SignalInsufficient:
            ++diagnostics.lattice_recovery_rejected_signal_count;
            break;
        case LatticeRecoveryDecision::StepUnsupported:
            ++diagnostics.lattice_recovery_rejected_step_count;
            break;
        case LatticeRecoveryDecision::GeometryUnsupported:
            ++diagnostics.lattice_recovery_rejected_geometry_count;
            break;
        case LatticeRecoveryDecision::Supported:
            break;
    }
}

void rejectNearbyFragments(
    std::vector<SpotObservation>& observations,
    const cv::Mat& centroid_intensity,
    const cv::Rect& roi_rect,
    const double background,
    const ProcessingConfig& config,
    ImageDiagnostics& diagnostics) {
    constexpr double kSubpitchFragmentDistanceFraction = 0.20;
    const double typical_spacing = medianNearestNeighborDistance(observations);
    std::vector<std::size_t> priority(observations.size());
    std::iota(priority.begin(), priority.end(), 0U);
    std::sort(priority.begin(), priority.end(), [&observations](const std::size_t left, const std::size_t right) {
        const auto& left_observation = observations[left];
        const auto& right_observation = observations[right];
        if (left_observation.area != right_observation.area) {
            return left_observation.area > right_observation.area;
        }
        if (left_observation.integrated_intensity != right_observation.integrated_intensity) {
            return left_observation.integrated_intensity > right_observation.integrated_intensity;
        }
        return left_observation.source_component_label < right_observation.source_component_label;
    });

    std::vector<bool> rejected(observations.size(), false);
    for (std::size_t primary_position = 0; primary_position < priority.size(); ++primary_position) {
        const std::size_t primary_index = priority[primary_position];
        if (rejected[primary_index]) {
            continue;
        }
        auto& primary = observations[primary_index];
        for (std::size_t candidate_position = primary_position + 1U;
             candidate_position < priority.size();
             ++candidate_position) {
            const std::size_t candidate_index = priority[candidate_position];
            if (rejected[candidate_index]) {
                continue;
            }
            auto& candidate = observations[candidate_index];
            const bool is_smaller_fragment =
                candidate.area <= primary.area * config.multispot_fragment_max_area_ratio;
            const double proximity_limit = config.multispot_fragment_proximity_factor *
                (std::sqrt(primary.area) + std::sqrt(candidate.area));
            const double center_distance = cv::norm(primary.center - candidate.center);
            const bool nearby = center_distance <= proximity_limit;
            const bool common_bright_bridge = nearby && hasCommonBrightBridge(
                primary, candidate, centroid_intensity, roi_rect, background);
            const bool subpitch_fragment = nearby && std::isfinite(typical_spacing) &&
                center_distance <= typical_spacing * kSubpitchFragmentDistanceFraction;
            if (is_smaller_fragment && (common_bright_bridge || subpitch_fragment)) {
                rejected[candidate_index] = true;
                ++diagnostics.rejected_proximity_count;
                addWarning(diagnostics, "NEARBY_FRAGMENT_REJECTED");
                if (subpitch_fragment && !common_bright_bridge) {
                    addFlag(primary, "SUBPITCH_FRAGMENT_NEIGHBOR_REJECTED");
                    addWarning(diagnostics, "SUBPITCH_FRAGMENT_REJECTED_UNVERIFIED");
                }
            } else if (nearby) {
                addFlag(primary, "NEARBY_CANDIDATE_UNRESOLVED");
                addFlag(candidate, "NEARBY_CANDIDATE_UNRESOLVED");
                addWarning(diagnostics, "NEARBY_CANDIDATE_UNRESOLVED");
            }
        }
    }

    std::vector<SpotObservation> kept;
    kept.reserve(observations.size() -
        static_cast<std::size_t>(diagnostics.rejected_proximity_count));
    for (std::size_t index = 0; index < observations.size(); ++index) {
        if (!rejected[index]) {
            kept.push_back(std::move(observations[index]));
        }
    }
    observations = std::move(kept);
}

void appendCountDetails(ErrorInfo& error, const ImageDiagnostics& diagnostics) {
    error.number_details["detected_count"] = diagnostics.candidate_count;
    error.number_details["raw_candidate_count"] = diagnostics.raw_candidate_count;
    error.number_details["rejected_area_count"] = diagnostics.rejected_area_count;
    error.number_details["rejected_absolute_area_count"] =
        diagnostics.rejected_absolute_area_count;
    error.number_details["rejected_relative_area_count"] =
        diagnostics.rejected_relative_area_count;
    error.number_details["rejected_zero_signal_count"] =
        diagnostics.rejected_zero_signal_count;
    error.number_details["rejected_border_count"] = diagnostics.rejected_border_count;
    error.number_details["rejected_shape_count"] = diagnostics.rejected_shape_count;
    error.number_details["rejected_proximity_count"] = diagnostics.rejected_proximity_count;
    error.number_details["lattice_recovery_considered_count"] =
        diagnostics.lattice_recovery_considered_count;
    error.number_details["lattice_recovery_rejected_signal_count"] =
        diagnostics.lattice_recovery_rejected_signal_count;
    error.number_details["lattice_recovery_rejected_step_count"] =
        diagnostics.lattice_recovery_rejected_step_count;
    error.number_details["lattice_recovery_rejected_geometry_count"] =
        diagnostics.lattice_recovery_rejected_geometry_count;
    error.number_details["lattice_recovered_count"] = diagnostics.lattice_recovered_count;
    error.number_details["background_intensity"] = diagnostics.background_intensity;
    error.number_details["detection_threshold"] = diagnostics.detection_threshold;
    error.string_details["segmentation_source"] = diagnostics.segmentation_source;
    error.string_details["centroid_intensity_source"] = diagnostics.centroid_intensity_source;
}

bool touchesMargin(
    const int left,
    const int top,
    const int width,
    const int height,
    const int image_width,
    const int image_height,
    const int margin) {
    return left <= margin || top <= margin ||
        left + width >= image_width - margin || top + height >= image_height - margin;
}

bool hasBrightEdgeContact(
    const cv::Mat& image,
    const cv::Mat& labels,
    const int component_label,
    const int left,
    const int top,
    const int width,
    const int height,
    const int probe_margin,
    const double signal_threshold) {
    cv::Point leftmost(image.cols, -1);
    cv::Point rightmost(-1, -1);
    cv::Point topmost(-1, image.rows);
    cv::Point bottommost(-1, -1);
    for (int y = top; y < top + height; ++y) {
        const int* label_row = labels.ptr<int>(y);
        for (int x = left; x < left + width; ++x) {
            if (label_row[x] != component_label) {
                continue;
            }
            if (x < leftmost.x) {
                leftmost = {x, y};
            }
            if (x > rightmost.x) {
                rightmost = {x, y};
            }
            if (y < topmost.y) {
                topmost = {x, y};
            }
            if (y > bottommost.y) {
                bottommost = {x, y};
            }
        }
    }
    if (leftmost.y < 0) {
        return false;
    }
    return (left <= probe_margin &&
            hasContinuousBrightLine(image, leftmost, {0, leftmost.y}, signal_threshold)) ||
        (left + width >= image.cols - probe_margin &&
         hasContinuousBrightLine(
             image, rightmost, {image.cols - 1, rightmost.y}, signal_threshold)) ||
        (top <= probe_margin &&
         hasContinuousBrightLine(image, topmost, {topmost.x, 0}, signal_threshold)) ||
        (top + height >= image.rows - probe_margin &&
         hasContinuousBrightLine(
             image, bottommost, {bottommost.x, image.rows - 1}, signal_threshold));
}

}  // namespace

ImageAnalysis MultispotDetector::detect(
    ImageAnalysis analysis,
    const ProcessingConfig& config) const try {
    if (analysis.gray.empty()) {
        analysis.error = makeError("IMAGE_LOAD_FAILED", "Experimental multispot input has no grayscale image.", true);
        return analysis;
    }

    if ((hasWarning(analysis.diagnostics, "IMAGE_UNDEREXPOSED") ||
         hasWarning(analysis.diagnostics, "IMAGE_OVEREXPOSED")) &&
        analysis.diagnostics.intensity_stddev < 4.0) {
        analysis.error = makeError(
            "SPOT_COUNT_MISMATCH",
            "Experimental multispot detection rejected an underexposed or overexposed image.",
            true);
        appendCountDetails(analysis.error, analysis.diagnostics);
        return analysis;
    }

    cv::Mat centroid_intensity;
    if (analysis.original.channels() == 1) {
        centroid_intensity = analysis.gray;
    } else {
        cv::extractChannel(analysis.original(analysis.roi_rect), centroid_intensity, 1);
        cv::Scalar green_stddev;
        cv::Scalar luminance_stddev;
        cv::meanStdDev(centroid_intensity, cv::noArray(), green_stddev);
        cv::meanStdDev(analysis.gray, cv::noArray(), luminance_stddev);
        if (green_stddev[0] < std::max(1.0, 0.20 * luminance_stddev[0])) {
            centroid_intensity = analysis.gray;
            analysis.diagnostics.centroid_intensity_source = "bt601_luminance_fallback";
            addWarning(analysis.diagnostics, "GREEN_CHANNEL_SIGNAL_WEAK");
        }
    }

    cv::Mat filtered;
    cv::Mat centroid_filtered;
    if (config.median_kernel > 1) {
        cv::medianBlur(analysis.gray, filtered, config.median_kernel);
        cv::medianBlur(centroid_intensity, centroid_filtered, config.median_kernel);
    } else {
        filtered = analysis.gray.clone();
        centroid_filtered = centroid_intensity.clone();
    }

    // A large odd top-hat kernel supplies a local-background residual. This is
    // intentionally an internal research default, not a recovered LM700 constant.
    const int top_hat_size = makeOddKernel(config.tophat_kernel);
    const cv::Mat top_hat_kernel = cv::getStructuringElement(
        cv::MORPH_ELLIPSE, cv::Size(top_hat_size, top_hat_size));
    cv::morphologyEx(filtered, analysis.enhanced, cv::MORPH_TOPHAT, top_hat_kernel);
    cv::Mat centroid_enhanced;
    cv::morphologyEx(
        centroid_filtered, centroid_enhanced, cv::MORPH_TOPHAT, top_hat_kernel);
    const double centroid_background = borderMean(centroid_enhanced);

    analysis.diagnostics.background_intensity = borderMean(analysis.enhanced);
    const double bright_cutoff =
        analysis.diagnostics.background_intensity * config.multispot_background_factor;
    cv::Mat bright_mask;
    cv::compare(analysis.enhanced, bright_cutoff, bright_mask, cv::CMP_GT);
    const int bright_count = cv::countNonZero(bright_mask);
    const double bright_mean = bright_count >= 31 ? cv::mean(analysis.enhanced, bright_mask)[0] : 0.0;
    const double high_percentile = percentile(analysis.enhanced, 0.995);
    analysis.diagnostics.detection_threshold = std::max({
        static_cast<double>(config.multispot_min_threshold),
        std::floor(bright_mean) - 16.0,
        analysis.diagnostics.background_intensity +
            0.20 * std::max(0.0, high_percentile - analysis.diagnostics.background_intensity),
    });

    cv::threshold(
        analysis.enhanced,
        analysis.binary,
        analysis.diagnostics.detection_threshold,
        255.0,
        cv::THRESH_BINARY);
    const cv::Mat cleanup_kernel = cv::getStructuringElement(
        cv::MORPH_ELLIPSE, cv::Size(3, 3));
    cv::morphologyEx(analysis.binary, analysis.binary, cv::MORPH_OPEN, cleanup_kernel);

    cv::Mat labels;
    cv::Mat stats;
    cv::Mat centers;
    const int label_count = cv::connectedComponentsWithStats(
        analysis.binary, labels, stats, centers, 8, CV_32S);
    analysis.diagnostics.raw_candidate_count = std::max(0, label_count - 1);

    const double maximum_area = std::max(
        static_cast<double>(config.multispot_min_area_pixels + 1),
        analysis.gray.total() * config.multispot_max_area_ratio);
    const double centroid_raw_background = percentile(centroid_intensity, 0.50);
    for (int label = 1; label < label_count; ++label) {
        const int area = stats.at<int>(label, cv::CC_STAT_AREA);
        const int left = stats.at<int>(label, cv::CC_STAT_LEFT);
        const int top = stats.at<int>(label, cv::CC_STAT_TOP);
        const int width = stats.at<int>(label, cv::CC_STAT_WIDTH);
        const int height = stats.at<int>(label, cv::CC_STAT_HEIGHT);
        if (area < config.multispot_min_area_pixels || area > maximum_area) {
            ++analysis.diagnostics.rejected_area_count;
            ++analysis.diagnostics.rejected_absolute_area_count;
            continue;
        }
        double integrated = 0.0;
        double weighted_x = 0.0;
        double weighted_y = 0.0;
        double raw_sum = 0.0;
        double peak = 0.0;
        double residual_peak = 0.0;
        double moment_xx = 0.0;
        double moment_xy = 0.0;
        double moment_yy = 0.0;
        const double component_center_x = centers.at<double>(label, 0);
        const double component_center_y = centers.at<double>(label, 1);
        for (int y = top; y < top + height; ++y) {
            const int* label_row = labels.ptr<int>(y);
            const unsigned char* centroid_enhanced_row =
                centroid_enhanced.ptr<unsigned char>(y);
            const unsigned char* intensity_row = centroid_intensity.ptr<unsigned char>(y);
            for (int x = left; x < left + width; ++x) {
                if (label_row[x] != label) {
                    continue;
                }
                const double residual = std::max(
                    0.0,
                    static_cast<double>(centroid_enhanced_row[x]) - centroid_background);
                integrated += residual;
                residual_peak = std::max(residual_peak, residual);
                weighted_x += residual * x;
                weighted_y += residual * y;
                raw_sum += intensity_row[x];
                peak = std::max(peak, static_cast<double>(intensity_row[x]));
                const double dx = static_cast<double>(x) - component_center_x;
                const double dy = static_cast<double>(y) - component_center_y;
                moment_xx += dx * dx;
                moment_xy += dx * dy;
                moment_yy += dy * dy;
            }
        }
        if (integrated <= 1e-9) {
            ++analysis.diagnostics.rejected_area_count;
            ++analysis.diagnostics.rejected_zero_signal_count;
            continue;
        }
        const double edge_signal_threshold = centroid_raw_background +
            std::max(8.0, 0.10 * std::max(0.0, peak - centroid_raw_background));
        const int edge_probe_margin = std::max(width, height);
        const bool segmented_support_touches_guard = touchesMargin(
            left,
            top,
            width,
            height,
            analysis.gray.cols,
            analysis.gray.rows,
            config.multispot_border_margin_pixels);
        if (segmented_support_touches_guard ||
            hasBrightEdgeContact(
                centroid_intensity,
                labels,
                label,
                left,
                top,
                width,
                height,
                edge_probe_margin,
                edge_signal_threshold)) {
            ++analysis.diagnostics.rejected_border_count;
            addWarning(analysis.diagnostics, "EDGE_CLIPPED_CANDIDATE_REJECTED");
            continue;
        }

        SpotObservation observation;
        observation.center = {
            weighted_x / integrated + analysis.roi_rect.x,
            weighted_y / integrated + analysis.roi_rect.y};
        observation.area = area;
        observation.circularity = static_cast<double>(area) / static_cast<double>(width * height);
        observation.bounding_box_elongation = std::max(
            static_cast<double>(width) / static_cast<double>(height),
            static_cast<double>(height) / static_cast<double>(width));
        const double covariance_xx = moment_xx / static_cast<double>(area);
        const double covariance_xy = moment_xy / static_cast<double>(area);
        const double covariance_yy = moment_yy / static_cast<double>(area);
        const double trace = covariance_xx + covariance_yy;
        const double discriminant = std::sqrt(
            std::max(0.0,
                (covariance_xx - covariance_yy) * (covariance_xx - covariance_yy) +
                    4.0 * covariance_xy * covariance_xy));
        const double largest_eigenvalue = std::max(0.0, 0.5 * (trace + discriminant));
        const double smallest_eigenvalue = std::max(1e-9, 0.5 * (trace - discriminant));
        observation.principal_axis_elongation =
            std::sqrt(largest_eigenvalue / smallest_eigenvalue);
        observation.mean_intensity = raw_sum / static_cast<double>(area);
        observation.peak_intensity = peak;
        observation.peak_residual_intensity = residual_peak;
        observation.integrated_intensity = integrated;
        observation.source_component_label = label;
        analysis.observations.push_back(std::move(observation));
    }

    analysis.diagnostics.candidate_count = static_cast<int>(analysis.observations.size());
    if (analysis.diagnostics.candidate_count > config.multispot_max_count) {
        analysis.diagnostics.candidate_limit_exceeded = true;
        addWarning(analysis.diagnostics, "SPOT_LIMIT_EXCEEDED");
        analysis.error = makeError(
            "SPOT_COUNT_MISMATCH",
            "Experimental multispot detection found more pre-filter candidates than the safe lattice-analysis limit.",
            true);
        analysis.error.number_details["max_spot_count"] = config.multispot_max_count;
        appendCountDetails(analysis.error, analysis.diagnostics);
        return analysis;
    }

    if (!analysis.observations.empty()) {
        const double preliminary_median_area = medianArea(analysis.observations);
        const double relative_minimum_area =
            preliminary_median_area * config.multispot_relative_min_area_ratio;
        std::vector<SpotObservation> anchors;
        anchors.reserve(analysis.observations.size());
        for (const auto& observation : analysis.observations) {
            if (observation.area >= relative_minimum_area && observation.circularity >= 0.35) {
                anchors.push_back(observation);
            }
        }
        const double typical_spacing = medianNearestNeighborDistance(anchors);
        const std::vector<cv::Point2d> lattice_steps =
            collectLatticeStepVectors(anchors, typical_spacing);
        const double median_integrated_intensity =
            anchors.empty() ? 0.0 : medianIntegratedIntensity(anchors);
        const double median_residual_peak =
            anchors.empty() ? 0.0 : medianResidualPeak(anchors);

        std::vector<SpotObservation> kept;
        std::vector<SpotObservation> pending_area_outliers;
        kept.reserve(analysis.observations.size());
        pending_area_outliers.reserve(analysis.observations.size());
        for (auto& observation : analysis.observations) {
            const bool area_outlier = observation.area < relative_minimum_area;
            const bool shape_outlier = observation.circularity < 0.35;
            if (shape_outlier) {
                if (area_outlier) {
                    ++analysis.diagnostics.rejected_area_count;
                    ++analysis.diagnostics.rejected_relative_area_count;
                    addWarning(analysis.diagnostics, "SMALL_AREA_OUTLIER_REJECTED");
                }
                ++analysis.diagnostics.rejected_shape_count;
                addWarning(analysis.diagnostics, "IRREGULAR_COMPONENT_REJECTED");
            } else if (area_outlier) {
                pending_area_outliers.push_back(std::move(observation));
            } else {
                kept.push_back(std::move(observation));
            }
        }

        while (!pending_area_outliers.empty()) {
            std::vector<bool> recovered(pending_area_outliers.size(), false);
            bool recovered_any = false;
            for (std::size_t index = 0; index < pending_area_outliers.size(); ++index) {
                recovered[index] = evaluateLatticeSupport(
                    pending_area_outliers[index],
                    anchors,
                    lattice_steps,
                    typical_spacing,
                    median_integrated_intensity,
                    median_residual_peak) == LatticeRecoveryDecision::Supported;
                recovered_any = recovered_any || recovered[index];
            }
            if (!recovered_any) {
                break;
            }

            std::vector<SpotObservation> unresolved;
            unresolved.reserve(pending_area_outliers.size());
            for (std::size_t index = 0; index < pending_area_outliers.size(); ++index) {
                auto& observation = pending_area_outliers[index];
                if (!recovered[index]) {
                    unresolved.push_back(std::move(observation));
                    continue;
                }
                addFlag(observation, "LATTICE_RECOVERED_UNVERIFIED");
                addWarning(analysis.diagnostics, "LATTICE_RECOVERED_UNVERIFIED");
                ++analysis.diagnostics.lattice_recovery_considered_count;
                ++analysis.diagnostics.lattice_recovered_count;
                analysis.diagnostics.lattice_recovered_centers.push_back(observation.center);
                anchors.push_back(observation);
                kept.push_back(std::move(observation));
            }
            pending_area_outliers = std::move(unresolved);
        }

        if (!pending_area_outliers.empty()) {
            for (const auto& observation : pending_area_outliers) {
                recordRejectedRecoveryDecision(
                    evaluateLatticeSupport(
                        observation,
                        anchors,
                        lattice_steps,
                        typical_spacing,
                        median_integrated_intensity,
                        median_residual_peak),
                    analysis.diagnostics);
            }
            analysis.diagnostics.rejected_area_count +=
                static_cast<int>(pending_area_outliers.size());
            analysis.diagnostics.rejected_relative_area_count +=
                static_cast<int>(pending_area_outliers.size());
            addWarning(analysis.diagnostics, "SMALL_AREA_OUTLIER_REJECTED");
        }
        analysis.observations = std::move(kept);
    }

    analysis.diagnostics.candidate_count = static_cast<int>(analysis.observations.size());
    if (analysis.diagnostics.candidate_count > config.multispot_max_count) {
        analysis.diagnostics.candidate_limit_exceeded = true;
        addWarning(analysis.diagnostics, "SPOT_LIMIT_EXCEEDED");
        analysis.error = makeError(
            "SPOT_COUNT_MISMATCH",
            "Experimental multispot detection found more candidates than the safe limit before proximity filtering.",
            true);
        analysis.error.number_details["max_spot_count"] = config.multispot_max_count;
        appendCountDetails(analysis.error, analysis.diagnostics);
        return analysis;
    }

    // A dark or saturated core can split one optical spot into nearby components.
    // Keep the larger support region and report the discarded satellite explicitly.
    rejectNearbyFragments(
        analysis.observations,
        centroid_intensity,
        analysis.roi_rect,
        centroid_raw_background,
        config,
        analysis.diagnostics);
    analysis.diagnostics.lattice_recovered_centers.erase(
        std::remove_if(
            analysis.diagnostics.lattice_recovered_centers.begin(),
            analysis.diagnostics.lattice_recovered_centers.end(),
            [&analysis](const cv::Point2d& recovered_center) {
                return std::none_of(
                    analysis.observations.begin(),
                    analysis.observations.end(),
                    [&recovered_center](const SpotObservation& observation) {
                        return observation.center == recovered_center;
                    });
            }),
        analysis.diagnostics.lattice_recovered_centers.end());
    analysis.diagnostics.lattice_recovered_count =
        static_cast<int>(analysis.diagnostics.lattice_recovered_centers.size());

    analysis.diagnostics.candidate_count = static_cast<int>(analysis.observations.size());
    if (analysis.diagnostics.candidate_count > config.multispot_max_count) {
        analysis.diagnostics.candidate_limit_exceeded = true;
        addWarning(analysis.diagnostics, "SPOT_LIMIT_EXCEEDED");
        analysis.error = makeError(
            "SPOT_COUNT_MISMATCH",
            "Experimental multispot detection found more candidates than the safe limit.",
            true);
        analysis.error.number_details["max_spot_count"] = config.multispot_max_count;
        appendCountDetails(analysis.error, analysis.diagnostics);
        return analysis;
    }
    if (analysis.diagnostics.candidate_count < config.multispot_min_count) {
        analysis.error = makeError(
            "SPOT_COUNT_MISMATCH",
            "Experimental multispot detection found fewer candidates than the usable minimum.",
            true);
        analysis.error.number_details["min_spot_count"] = config.multispot_min_count;
        appendCountDetails(analysis.error, analysis.diagnostics);
        return analysis;
    }

    const double median_area = medianArea(analysis.observations);
    bool possible_merged_component = false;
    for (auto& observation : analysis.observations) {
        const double area_ratio = std::max(observation.area / median_area, 1e-9);
        const double area_score = std::exp(-std::abs(std::log(area_ratio)));
        const double shape_score = clamp01(observation.circularity / 0.70);
        const double residual_signal_score = clamp01(
            observation.peak_residual_intensity /
            std::max(1.0, 255.0 - centroid_background));
        const double raw_peak_score = clamp01(observation.peak_intensity / 255.0);
        const double signal_score = 0.5 * residual_signal_score + 0.5 * raw_peak_score;
        observation.confidence = clamp01(
            0.45 * signal_score + 0.30 * shape_score + 0.25 * area_score);
        const bool large_area =
            observation.area > median_area * config.multispot_merged_area_ratio;
        const bool elongated =
            observation.principal_axis_elongation > config.multispot_merged_elongation_ratio;
        if (large_area && elongated) {
            addFlag(observation, "POSSIBLE_MERGED_COMPONENT");
            addWarning(analysis.diagnostics, "POSSIBLE_MERGED_COMPONENT");
            possible_merged_component = true;
        } else if (large_area) {
            addFlag(observation, "AREA_ABOVE_MEDIAN");
            addWarning(analysis.diagnostics, "AREA_VARIATION_HIGH");
        }
        if (observation.peak_intensity >= 253.0) {
            addFlag(observation, "SATURATED_PEAK");
            addWarning(analysis.diagnostics, "SATURATED_PEAK");
        }
        if (observation.confidence < config.multispot_min_confidence) {
            addFlag(observation, "LOW_CONFIDENCE");
            addWarning(analysis.diagnostics, "LOW_CONFIDENCE_CANDIDATE");
        }
    }
    if (possible_merged_component) {
        analysis.error = makeError(
            "CENTROID_FAILED",
            "Experimental multispot detection found a possible merged component and will not guess centers.",
            true);
        appendCountDetails(analysis.error, analysis.diagnostics);
        return analysis;
    }

    std::sort(
        analysis.observations.begin(),
        analysis.observations.end(),
        [](const SpotObservation& left, const SpotObservation& right) {
            if (left.center.y != right.center.y) {
                return left.center.y < right.center.y;
            }
            if (left.center.x != right.center.x) {
                return left.center.x < right.center.x;
            }
            return left.source_component_label < right.source_component_label;
        });

    cv::rectangle(analysis.annotated, analysis.roi_rect, cv::Scalar(255, 160, 0), 1, cv::LINE_AA);
    for (std::size_t index = 0; index < analysis.observations.size(); ++index) {
        const auto& center = analysis.observations[index].center;
        cv::circle(analysis.annotated, center, 8, cv::Scalar(80, 255, 80), 1, cv::LINE_AA);
        cv::putText(
            analysis.annotated,
            "d" + std::to_string(index),
            center + cv::Point2d(7.0, -7.0),
            cv::FONT_HERSHEY_SIMPLEX,
            0.35,
            cv::Scalar(80, 255, 80),
            1,
            cv::LINE_AA);
    }
    return analysis;
} catch (const cv::Exception& exception) {
    analysis.error = makeError(
        "IMAGE_LOAD_FAILED", "OpenCV failed during experimental multispot detection.", true);
    analysis.error.string_details["reason"] = exception.what();
    return analysis;
} catch (const std::exception& exception) {
    analysis.error = makeError(
        "UNKNOWN_ERROR", "Experimental multispot detection stopped unexpectedly.", false);
    analysis.error.string_details["reason"] = exception.what();
    return analysis;
}

}  // namespace focimeter::m2
