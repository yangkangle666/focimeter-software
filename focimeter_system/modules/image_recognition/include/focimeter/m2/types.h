#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include <opencv2/core.hpp>

namespace focimeter::m2 {

struct ErrorInfo {
    std::string code;
    std::string message;
    bool recoverable{false};
    std::map<std::string, std::string> string_details;
    std::map<std::string, double> number_details;

    [[nodiscard]] bool empty() const noexcept { return code.empty(); }
};

struct ProcessingConfig {
    double roi_width_ratio{0.9};
    double roi_height_ratio{0.9};
    int median_kernel{3};
    int tophat_kernel{30};
    double otsu_a{0.4};
    double otsu_b{0.7};
    int max_depth{2};
    int expected_spot_count{5};
    double min_confidence{0.7};

    // 统一配置暂未定义配对参数；这些是保守的第一阶段工程门限。
    double max_rotation_degrees{35.0};
    double min_scale{0.70};
    double max_scale{1.30};
    double max_residual_ratio{0.12};
    double ambiguity_ratio{1.15};
};

struct SpotObservation {
    cv::Point2d center;
    double area{0.0};
    double circularity{0.0};
    double mean_intensity{0.0};
    double confidence{0.0};
};

struct Spot {
    int spot_id{-1};
    std::string role{"unknown"};
    cv::Point2d center;
    double confidence{0.0};
};

struct ImageAnalysis {
    std::vector<SpotObservation> observations;
    cv::Rect roi_rect;
    cv::Mat original;
    cv::Mat gray;
    cv::Mat enhanced;
    cv::Mat binary;
    cv::Mat annotated;
    ErrorInfo error;

    [[nodiscard]] bool ok() const noexcept { return error.empty(); }
};

struct MatchDiagnostics {
    double scale{1.0};
    double rotation_degrees{0.0};
    double residual_pixels{0.0};
    double residual_limit_pixels{0.0};
};

struct InputPackage {
    std::string schema_version;
    std::string task_id;
    std::filesystem::path calibration_image;
    std::filesystem::path measurement_image;
    std::filesystem::path config_path;
    std::string run_mode;
};

struct RunOptions {
    std::filesystem::path input_package;
    std::filesystem::path output_directory;
    std::filesystem::path project_root;
    bool save_intermediate{false};
};

struct RunResult {
    int exit_code{1};
    std::filesystem::path calibration_output;
    std::filesystem::path measurement_output;
    std::filesystem::path log_output;
    ErrorInfo error;

    [[nodiscard]] bool ok() const noexcept { return exit_code == 0 && error.empty(); }
};

}  // namespace focimeter::m2
