#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <opencv2/core.hpp>

namespace focimeter::m2 {

enum class RecognitionMode {
    FiveSpotCompat,
    HartmannMultispotExperimental,
};

struct ErrorInfo {
    std::string code;
    std::string message;
    bool recoverable{false};
    std::map<std::string, std::string> string_details;
    std::map<std::string, double> number_details;

    [[nodiscard]] bool empty() const noexcept { return code.empty(); }
};

struct ProcessingConfig {
    std::optional<int> declared_image_width;
    std::optional<int> declared_image_height;
    double roi_width_ratio{0.9};
    double roi_height_ratio{0.9};
    int median_kernel{3};
    int tophat_kernel{30};
    double otsu_a{0.4};
    double otsu_b{0.7};
    int max_depth{2};
    int expected_spot_count{5};
    double min_confidence{0.7};
    std::string data_source{"unknown"};

    // 统一配置暂未定义配对参数；这些是保守的第一阶段工程门限。
    double max_rotation_degrees{35.0};
    double min_scale{0.70};
    double max_scale{1.30};
    double max_residual_ratio{0.12};
    double ambiguity_ratio{1.15};

    // 以下参数仅服务于 M2 内部实验模式，不来自也不修改统一配置文件。
    RecognitionMode recognition_mode{RecognitionMode::FiveSpotCompat};
    int multispot_min_count{12};
    int multispot_max_count{150};
    int multispot_min_area_pixels{12};
    double multispot_max_area_ratio{0.02};
    double multispot_relative_min_area_ratio{0.35};
    double multispot_fragment_proximity_factor{1.25};
    double multispot_fragment_max_area_ratio{0.65};
    double multispot_merged_area_ratio{1.5};
    double multispot_merged_elongation_ratio{1.35};
    int multispot_border_margin_pixels{2};
    double multispot_background_factor{1.3};
    int multispot_min_threshold{8};
    double multispot_min_confidence{0.35};
    std::optional<int> multispot_16bit_white_level;
};

struct SpotObservation {
    cv::Point2d center;
    double area{0.0};
    double circularity{0.0};
    double bounding_box_elongation{1.0};
    double principal_axis_elongation{1.0};
    double mean_intensity{0.0};
    double peak_intensity{0.0};
    double peak_residual_intensity{0.0};
    double integrated_intensity{0.0};
    double confidence{0.0};
    int source_component_label{-1};
    std::vector<std::string> quality_flags;
};

struct Spot {
    int spot_id{-1};
    std::string role{"unknown"};
    cv::Point2d center;
    double confidence{0.0};
};

struct ImageDiagnostics {
    int image_width{0};
    int image_height{0};
    int channels{0};
    int source_depth_bits{0};
    double normalization_white_level{255.0};
    double mean_intensity{0.0};
    double intensity_stddev{0.0};
    double minimum_intensity{0.0};
    double maximum_intensity{0.0};
    double dark_pixel_ratio{0.0};
    double bright_pixel_ratio{0.0};
    int candidate_count{0};
    int raw_candidate_count{0};
    int rejected_area_count{0};
    int rejected_absolute_area_count{0};
    int rejected_relative_area_count{0};
    int rejected_zero_signal_count{0};
    int rejected_border_count{0};
    int rejected_shape_count{0};
    int rejected_proximity_count{0};
    int lattice_recovered_count{0};
    double background_intensity{0.0};
    double detection_threshold{0.0};
    bool candidate_limit_exceeded{false};
    std::string segmentation_source{"native_grayscale"};
    std::string centroid_intensity_source{"native_grayscale"};
    std::vector<cv::Point2d> lattice_recovered_centers;
    std::vector<std::string> warnings;
};

struct ImageAnalysis {
    std::vector<SpotObservation> observations;
    ImageDiagnostics diagnostics;
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
    std::string data_source{"unknown"};
};

struct RunOptions {
    std::filesystem::path input_package;
    std::filesystem::path output_directory;
    std::filesystem::path project_root;
    RecognitionMode recognition_mode{RecognitionMode::FiveSpotCompat};
    std::optional<int> experimental_16bit_white_level;
    bool save_intermediate{false};
};

struct RunResult {
    int exit_code{1};
    std::filesystem::path calibration_output;
    std::filesystem::path measurement_output;
    std::filesystem::path log_output;
    RecognitionMode recognition_mode{RecognitionMode::FiveSpotCompat};
    ErrorInfo error;

    [[nodiscard]] bool ok() const noexcept { return exit_code == 0 && error.empty(); }
};

}  // namespace focimeter::m2
