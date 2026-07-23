#include "focimeter/m2/image_processor.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <fstream>
#include <iterator>
#include <numeric>
#include <vector>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

namespace focimeter::m2 {
namespace {

constexpr double kPi = 3.14159265358979323846;

double clamp01(const double value) {
    return std::clamp(value, 0.0, 1.0);
}

ErrorInfo makeError(
    std::string code,
    std::string message,
    const bool recoverable) {
    ErrorInfo error;
    error.code = std::move(code);
    error.message = std::move(message);
    error.recoverable = recoverable;
    return error;
}

bool validateImageConfig(const ProcessingConfig& config, ErrorInfo& error) {
    if (!std::isfinite(config.roi_width_ratio) ||
        !std::isfinite(config.roi_height_ratio) ||
        !std::isfinite(config.otsu_a) ||
        !std::isfinite(config.otsu_b) ||
        !std::isfinite(config.min_confidence) ||
        config.roi_width_ratio <= 0.0 || config.roi_width_ratio > 1.0 ||
        config.roi_height_ratio <= 0.0 || config.roi_height_ratio > 1.0 ||
        config.median_kernel < 1 || config.median_kernel % 2 == 0 ||
        config.tophat_kernel < 1 || config.tophat_kernel > 4096 ||
        config.otsu_a <= 0.0 || config.otsu_a >= config.otsu_b ||
        config.otsu_b > 1.0 || config.max_depth < 0 || config.max_depth > 8 ||
        config.expected_spot_count != 5 ||
        config.min_confidence < 0.0 || config.min_confidence > 1.0) {
        error = makeError(
            "CONFIG_INVALID",
            "Processing configuration values are outside M2's supported range.",
            true);
        return false;
    }
    return true;
}

bool convertToEightBit(const cv::Mat& source, cv::Mat& destination, ErrorInfo& error) {
    if (source.depth() == CV_8U) {
        destination = source.clone();
        return true;
    }
    if (source.depth() != CV_16U) {
        error = makeError(
            "IMAGE_LOAD_FAILED",
            "M2 supports only 8-bit and unsigned 16-bit images.",
            true);
        error.number_details["opencv_depth"] = source.depth();
        return false;
    }

    const cv::Mat contiguous = source.isContinuous() ? source : source.clone();
    const cv::Mat scalar_values = contiguous.reshape(1);
    double minimum = 0.0;
    double maximum = 0.0;
    cv::minMaxLoc(scalar_values, &minimum, &maximum);
    if (maximum > minimum) {
        const double scale = 255.0 / (maximum - minimum);
        source.convertTo(destination, CV_8U, scale, -minimum * scale);
    } else {
        source.convertTo(destination, CV_8U, 1.0 / 257.0);
    }
    return true;
}

double medianArea(const std::vector<SpotObservation>& observations) {
    std::vector<double> areas;
    areas.reserve(observations.size());
    for (const auto& observation : observations) {
        areas.push_back(observation.area);
    }
    std::sort(areas.begin(), areas.end());
    return areas[areas.size() / 2];
}

cv::Mat toDisplayBgr(const cv::Mat& image) {
    if (image.channels() == 3) {
        return image.clone();
    }

    cv::Mat bgr;
    if (image.channels() == 1) {
        cv::cvtColor(image, bgr, cv::COLOR_GRAY2BGR);
    } else if (image.channels() == 4) {
        cv::cvtColor(image, bgr, cv::COLOR_BGRA2BGR);
    }
    return bgr;
}

cv::Mat computeContrastMap(const cv::Mat& gray) {
    cv::Mat horizontal;
    cv::Mat vertical;
    cv::Sobel(gray, horizontal, CV_32F, 1, 0, 3);
    cv::Sobel(gray, vertical, CV_32F, 0, 1, 3);
    cv::Mat contrast;
    cv::magnitude(horizontal, vertical, contrast);
    return contrast;
}

double maximumContrast(const cv::Mat& contrast, const cv::Rect& region) {
    double maximum = 0.0;
    cv::minMaxLoc(contrast(region), nullptr, &maximum);
    return maximum;
}

void partitionOtsu(
    const cv::Mat& gray,
    const cv::Mat& contrast,
    cv::Mat& binary,
    const cv::Rect& region,
    const double parent_maximum,
    const ProcessingConfig& config,
    const int depth) {
    const double region_maximum = maximumContrast(contrast, region);
    if (region_maximum <= config.otsu_a * parent_maximum) {
        binary(region).setTo(0);
        return;
    }

    if (depth >= config.max_depth || region.width <= 4 || region.height <= 4) {
        cv::threshold(
            gray(region),
            binary(region),
            0.0,
            255.0,
            cv::THRESH_BINARY | cv::THRESH_OTSU);
        return;
    }

    const int left_width = region.width / 2;
    const int top_height = region.height / 2;
    const std::array<cv::Rect, 4> children{
        cv::Rect(region.x, region.y, left_width, top_height),
        cv::Rect(region.x + left_width, region.y, region.width - left_width, top_height),
        cv::Rect(region.x, region.y + top_height, left_width, region.height - top_height),
        cv::Rect(
            region.x + left_width,
            region.y + top_height,
            region.width - left_width,
            region.height - top_height),
    };
    for (const auto& child : children) {
        const double child_maximum = maximumContrast(contrast, child);
        if (child_maximum <= config.otsu_a * region_maximum) {
            binary(child).setTo(0);
        } else if (child_maximum >= config.otsu_b * region_maximum) {
            cv::threshold(
                gray(child),
                binary(child),
                0.0,
                255.0,
                cv::THRESH_BINARY | cv::THRESH_OTSU);
        } else {
            partitionOtsu(gray, contrast, binary, child, region_maximum, config, depth + 1);
        }
    }
}

cv::Mat adaptiveOtsu(const cv::Mat& gray, const ProcessingConfig& config) {
    const cv::Mat contrast = computeContrastMap(gray);
    const cv::Rect full_region(0, 0, gray.cols, gray.rows);
    const double global_maximum = maximumContrast(contrast, full_region);
    cv::Mat binary = cv::Mat::zeros(gray.size(), CV_8UC1);
    if (global_maximum > 0.0) {
        partitionOtsu(gray, contrast, binary, full_region, global_maximum, config, 0);
    }
    return binary;
}

}  // namespace

cv::Mat ImageProcessor::readImage(const std::filesystem::path& image_path) {
    // imread 在部分 Windows 环境下无法稳定处理中文路径，先按二进制读取再解码。
    std::ifstream stream(image_path, std::ios::binary);
    if (!stream) {
        return {};
    }

    const std::vector<unsigned char> bytes{
        std::istreambuf_iterator<char>(stream),
        std::istreambuf_iterator<char>()};
    if (bytes.empty()) {
        return {};
    }
    return cv::imdecode(bytes, cv::IMREAD_UNCHANGED);
}

ImageAnalysis ImageProcessor::processFile(
    const std::filesystem::path& image_path,
    const ProcessingConfig& config) const try {
    ImageAnalysis analysis;
    std::error_code path_error;
    const bool is_regular_file = std::filesystem::is_regular_file(image_path, path_error);
    if (!is_regular_file &&
        (!path_error || path_error == std::errc::no_such_file_or_directory)) {
        analysis.error = makeError(
            "IMAGE_NOT_FOUND",
            "Image file does not exist.",
            true);
        analysis.error.string_details["image_path"] = image_path.generic_string();
        return analysis;
    }
    if (path_error) {
        analysis.error = makeError(
            "IMAGE_LOAD_FAILED",
            "Image file metadata could not be read.",
            true);
        analysis.error.string_details["image_path"] = image_path.generic_string();
        analysis.error.string_details["reason"] = path_error.message();
        return analysis;
    }

    const cv::Mat image = readImage(image_path);
    if (image.empty()) {
        analysis.error = makeError(
            "IMAGE_LOAD_FAILED",
            "OpenCV could not decode the image file.",
            true);
        analysis.error.string_details["image_path"] = image_path.generic_string();
        return analysis;
    }
    return processMat(image, config);
} catch (const cv::Exception& exception) {
    ImageAnalysis analysis;
    analysis.error = makeError("IMAGE_LOAD_FAILED", "OpenCV failed while decoding the image.", true);
    analysis.error.string_details["reason"] = exception.what();
    analysis.error.string_details["image_path"] = image_path.generic_string();
    return analysis;
} catch (const std::exception& exception) {
    ImageAnalysis analysis;
    analysis.error = makeError("IMAGE_LOAD_FAILED", "The image file could not be read safely.", true);
    analysis.error.string_details["reason"] = exception.what();
    analysis.error.string_details["image_path"] = image_path.generic_string();
    return analysis;
}

ImageAnalysis ImageProcessor::processMat(
    const cv::Mat& image,
    const ProcessingConfig& config) const try {
    ImageAnalysis analysis;
    if (!validateImageConfig(config, analysis.error)) {
        return analysis;
    }
    if (image.empty()) {
        analysis.error = makeError(
            "IMAGE_LOAD_FAILED",
            "Input image is empty.",
            true);
        return analysis;
    }
    if (image.channels() != 1 && image.channels() != 3 && image.channels() != 4) {
        analysis.error = makeError(
            "IMAGE_LOAD_FAILED",
            "Only 1-channel, 3-channel, or 4-channel images are supported.",
            false);
        analysis.error.number_details["channels"] = image.channels();
        return analysis;
    }

    cv::Mat working_image;
    if (!convertToEightBit(image, working_image, analysis.error)) {
        return analysis;
    }
    analysis.original = working_image.clone();
    analysis.annotated = toDisplayBgr(working_image);

    const int roi_width = std::clamp(
        static_cast<int>(std::lround(working_image.cols * config.roi_width_ratio)),
        1,
        working_image.cols);
    const int roi_height = std::clamp(
        static_cast<int>(std::lround(working_image.rows * config.roi_height_ratio)),
        1,
        working_image.rows);
    analysis.roi_rect = cv::Rect(
        (working_image.cols - roi_width) / 2,
        (working_image.rows - roi_height) / 2,
        roi_width,
        roi_height);

    const cv::Mat roi = working_image(analysis.roi_rect);
    if (roi.channels() == 1) {
        analysis.gray = roi.clone();
    } else if (roi.channels() == 3) {
        cv::cvtColor(roi, analysis.gray, cv::COLOR_BGR2GRAY);
    } else {
        cv::cvtColor(roi, analysis.gray, cv::COLOR_BGRA2GRAY);
    }

    cv::Mat filtered;
    if (config.median_kernel > 1) {
        cv::medianBlur(analysis.gray, filtered, config.median_kernel);
    } else {
        filtered = analysis.gray.clone();
    }

    if (config.tophat_kernel > 1) {
        const cv::Mat kernel = cv::getStructuringElement(
            cv::MORPH_ELLIPSE,
            cv::Size(config.tophat_kernel, config.tophat_kernel));
        cv::morphologyEx(filtered, analysis.enhanced, cv::MORPH_TOPHAT, kernel);
    } else {
        analysis.enhanced = filtered.clone();
    }

    // 使用统一配置中的区域阈值参数，避免局部亮度变化淹没较暗光斑。
    analysis.binary = adaptiveOtsu(analysis.enhanced, config);
    const cv::Mat cleanup_kernel = cv::getStructuringElement(
        cv::MORPH_ELLIPSE,
        cv::Size(3, 3));
    cv::morphologyEx(
        analysis.binary,
        analysis.binary,
        cv::MORPH_OPEN,
        cleanup_kernel);

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(
        analysis.binary.clone(),
        contours,
        cv::RETR_EXTERNAL,
        cv::CHAIN_APPROX_SIMPLE);

    const double roi_area = static_cast<double>(roi_width) * roi_height;
    const double minimum_area = std::max(12.0, roi_area * 0.00002);
    const double maximum_area = roi_area * 0.05;

    for (const auto& contour : contours) {
        const cv::Rect bounds = cv::boundingRect(contour);
        if (bounds.x <= 0 || bounds.y <= 0 ||
            bounds.x + bounds.width >= roi_width ||
            bounds.y + bounds.height >= roi_height) {
            continue;
        }
        const double area = cv::contourArea(contour);
        const double perimeter = cv::arcLength(contour, true);
        if (area < minimum_area || area > maximum_area || perimeter <= 0.0) {
            continue;
        }

        const double circularity = 4.0 * kPi * area / (perimeter * perimeter);
        if (circularity < 0.45) {
            continue;
        }

        cv::Mat mask = cv::Mat::zeros(analysis.gray.size(), CV_8UC1);
        cv::drawContours(mask, std::vector<std::vector<cv::Point>>{contour}, 0, 255, cv::FILLED);

        cv::Mat weighted = cv::Mat::zeros(analysis.gray.size(), analysis.gray.type());
        analysis.gray.copyTo(weighted, mask);
        const cv::Moments intensity_moments = cv::moments(weighted, false);

        cv::Point2d local_center;
        if (std::abs(intensity_moments.m00) > 1e-9) {
            local_center.x = intensity_moments.m10 / intensity_moments.m00;
            local_center.y = intensity_moments.m01 / intensity_moments.m00;
        } else {
            const cv::Moments contour_moments = cv::moments(contour, false);
            if (std::abs(contour_moments.m00) <= 1e-9) {
                continue;
            }
            local_center.x = contour_moments.m10 / contour_moments.m00;
            local_center.y = contour_moments.m01 / contour_moments.m00;
        }

        SpotObservation observation;
        observation.center = cv::Point2d(
            local_center.x + analysis.roi_rect.x,
            local_center.y + analysis.roi_rect.y);
        observation.area = area;
        observation.circularity = circularity;
        observation.mean_intensity = cv::mean(analysis.gray, mask)[0];
        analysis.observations.push_back(observation);
    }

    if (!analysis.observations.empty()) {
        const double median_area = medianArea(analysis.observations);
        for (auto& observation : analysis.observations) {
            const double shape_score = clamp01((observation.circularity - 0.40) / 0.60);
            const double brightness_score = clamp01(observation.mean_intensity / 255.0);
            const double area_ratio = std::max(observation.area / median_area, 1e-9);
            const double area_score = std::exp(-std::abs(std::log(area_ratio)));
            observation.confidence = clamp01(
                0.45 * shape_score +
                0.35 * brightness_score +
                0.20 * area_score);
        }
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

    for (std::size_t index = 0; index < analysis.observations.size(); ++index) {
        const auto& point = analysis.observations[index].center;
        cv::circle(analysis.annotated, point, 9, cv::Scalar(0, 220, 255), 2, cv::LINE_AA);
        cv::putText(
            analysis.annotated,
            "candidate " + std::to_string(index),
            point + cv::Point2d(8.0, -8.0),
            cv::FONT_HERSHEY_SIMPLEX,
            0.45,
            cv::Scalar(0, 220, 255),
            1,
            cv::LINE_AA);
    }

    if (static_cast<int>(analysis.observations.size()) != config.expected_spot_count) {
        analysis.error = makeError(
            "SPOT_COUNT_MISMATCH",
            "Detected spot count does not match the configured expected count.",
            true);
        analysis.error.number_details["expected_count"] = config.expected_spot_count;
        analysis.error.number_details["detected_count"] =
            static_cast<double>(analysis.observations.size());
        return analysis;
    }

    const auto low_confidence = std::find_if(
        analysis.observations.begin(),
        analysis.observations.end(),
        [&config](const SpotObservation& observation) {
            return observation.confidence < config.min_confidence;
        });
    if (low_confidence != analysis.observations.end()) {
        analysis.error = makeError(
            "CENTROID_FAILED",
            "At least one detected spot is below the configured confidence threshold.",
            true);
        analysis.error.number_details["min_confidence"] = config.min_confidence;
        analysis.error.number_details["detected_confidence"] = low_confidence->confidence;
    }
    return analysis;
} catch (const cv::Exception& exception) {
    ImageAnalysis analysis;
    analysis.error = makeError("IMAGE_LOAD_FAILED", "OpenCV failed while processing the image.", true);
    analysis.error.string_details["reason"] = exception.what();
    return analysis;
} catch (const std::exception& exception) {
    ImageAnalysis analysis;
    analysis.error = makeError("IMAGE_LOAD_FAILED", "The image could not be processed safely.", true);
    analysis.error.string_details["reason"] = exception.what();
    return analysis;
}

}  // namespace focimeter::m2
