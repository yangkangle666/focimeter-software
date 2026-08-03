#include "focimeter/m2/multispot_detector.h"

#include <algorithm>
#include <cmath>
#include <exception>
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

void appendCountDetails(ErrorInfo& error, const ImageDiagnostics& diagnostics) {
    error.number_details["detected_count"] = diagnostics.candidate_count;
    error.number_details["raw_candidate_count"] = diagnostics.raw_candidate_count;
    error.number_details["rejected_area_count"] = diagnostics.rejected_area_count;
    error.number_details["rejected_border_count"] = diagnostics.rejected_border_count;
    error.number_details["background_intensity"] = diagnostics.background_intensity;
    error.number_details["detection_threshold"] = diagnostics.detection_threshold;
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

    cv::Mat filtered;
    if (config.median_kernel > 1) {
        cv::medianBlur(analysis.gray, filtered, config.median_kernel);
    } else {
        filtered = analysis.gray.clone();
    }

    // A large odd top-hat kernel supplies a local-background residual. This is
    // intentionally an internal research default, not a recovered LM700 constant.
    const int top_hat_size = makeOddKernel(config.tophat_kernel);
    const cv::Mat top_hat_kernel = cv::getStructuringElement(
        cv::MORPH_ELLIPSE, cv::Size(top_hat_size, top_hat_size));
    cv::morphologyEx(filtered, analysis.enhanced, cv::MORPH_TOPHAT, top_hat_kernel);

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
    for (int label = 1; label < label_count; ++label) {
        const int area = stats.at<int>(label, cv::CC_STAT_AREA);
        const int left = stats.at<int>(label, cv::CC_STAT_LEFT);
        const int top = stats.at<int>(label, cv::CC_STAT_TOP);
        const int width = stats.at<int>(label, cv::CC_STAT_WIDTH);
        const int height = stats.at<int>(label, cv::CC_STAT_HEIGHT);
        if (area < config.multispot_min_area_pixels || area > maximum_area) {
            ++analysis.diagnostics.rejected_area_count;
            continue;
        }
        if (touchesMargin(
                left,
                top,
                width,
                height,
                analysis.gray.cols,
                analysis.gray.rows,
                config.multispot_border_margin_pixels)) {
            ++analysis.diagnostics.rejected_border_count;
            addWarning(analysis.diagnostics, "EDGE_CLIPPED_CANDIDATE_REJECTED");
            continue;
        }

        double integrated = 0.0;
        double weighted_x = 0.0;
        double weighted_y = 0.0;
        double raw_sum = 0.0;
        double peak = 0.0;
        double residual_peak = 0.0;
        for (int y = top; y < top + height; ++y) {
            const int* label_row = labels.ptr<int>(y);
            const unsigned char* enhanced_row = analysis.enhanced.ptr<unsigned char>(y);
            const unsigned char* gray_row = analysis.gray.ptr<unsigned char>(y);
            for (int x = left; x < left + width; ++x) {
                if (label_row[x] != label) {
                    continue;
                }
                const double residual = std::max(
                    0.0,
                    static_cast<double>(enhanced_row[x]) - analysis.diagnostics.detection_threshold);
                integrated += residual;
                residual_peak = std::max(residual_peak, residual);
                weighted_x += residual * x;
                weighted_y += residual * y;
                raw_sum += gray_row[x];
                peak = std::max(peak, static_cast<double>(gray_row[x]));
            }
        }
        if (integrated <= 1e-9) {
            ++analysis.diagnostics.rejected_area_count;
            continue;
        }

        SpotObservation observation;
        observation.center = {
            weighted_x / integrated + analysis.roi_rect.x,
            weighted_y / integrated + analysis.roi_rect.y};
        observation.area = area;
        observation.circularity = static_cast<double>(area) / static_cast<double>(width * height);
        observation.mean_intensity = raw_sum / static_cast<double>(area);
        observation.peak_intensity = peak;
        observation.peak_residual_intensity = residual_peak;
        observation.integrated_intensity = integrated;
        analysis.observations.push_back(std::move(observation));
    }

    analysis.diagnostics.candidate_count = static_cast<int>(analysis.observations.size());
    if (analysis.diagnostics.rejected_border_count > 0) {
        analysis.error = makeError(
            "CENTROID_FAILED",
            "Experimental multispot detection rejected an edge-clipped candidate.",
            true);
        appendCountDetails(analysis.error, analysis.diagnostics);
        return analysis;
    }
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
        const double signal_score = clamp01(
            observation.peak_residual_intensity /
            std::max(1.0, 255.0 - analysis.diagnostics.detection_threshold));
        observation.confidence = clamp01(
            0.45 * signal_score + 0.30 * shape_score + 0.25 * area_score);
        if (observation.area > median_area * 1.5 || observation.circularity < 0.35) {
            addFlag(observation, "POSSIBLE_MERGED_COMPONENT");
            addWarning(analysis.diagnostics, "POSSIBLE_MERGED_COMPONENT");
            possible_merged_component = true;
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
            if (std::abs(left.center.y - right.center.y) > 1e-6) {
                return left.center.y < right.center.y;
            }
            return left.center.x < right.center.x;
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
