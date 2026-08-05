#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

namespace {

using Json = nlohmann::json;
using PointList = std::vector<cv::Point2d>;

constexpr int kImageWidth = 1280;
constexpr int kImageHeight = 1024;
constexpr std::uint64_t kSeed = 20260727ULL;
constexpr char kGeneratorVersion[] = "m2_multispot_synthetic_1.4.0";
constexpr char kDatasetProjectPath[] = "data/mock/m2_image_recognition/synthetic_multispot";

struct RenderOptions {
    double background{12.0};
    double gradient_x{0.0};
    double gradient_y{0.0};
    double noise_half_amplitude{0.0};
    double spot_amplitude{210.0};
    double spot_sigma{6.0};
    std::uint64_t noise_seed{kSeed};
};

struct ExpectedOutcome {
    std::string status{"ok"};
    std::string error_code;
    std::vector<std::string> warnings;
};

struct CaseSpec {
    std::string id;
    std::string package_file;
    std::string calibration_file;
    std::string measurement_file;
    PointList calibration_centers;
    PointList measurement_centers;
    int expected_calibration_count{0};
    int expected_measurement_count{0};
    Json transform;
    ExpectedOutcome expected;
    int input_white_level{0};
};

PointList makeGrid(const int rows, const int columns, const double start_x, const double start_y, const double spacing_x,
                   const double spacing_y) {
    PointList points;
    points.reserve(static_cast<std::size_t>(rows * columns));
    for (int row = 0; row < rows; ++row) {
        for (int column = 0; column < columns; ++column) {
            points.emplace_back(start_x + spacing_x * column, start_y + spacing_y * row);
        }
    }
    return points;
}

PointList make25PointGrid() {
    return makeGrid(5, 5, 420.0, 292.0, 110.0, 110.0);
}

PointList make94PointGrid() {
    const PointList full_grid = makeGrid(10, 10, 235.0, 107.0, 90.0, 90.0);
    const std::vector<int> omitted_indices{0, 9, 44, 55, 90, 99};
    PointList points;
    points.reserve(94);
    for (int index = 0; index < static_cast<int>(full_grid.size()); ++index) {
        if (std::find(omitted_indices.begin(), omitted_indices.end(), index) == omitted_indices.end()) {
            points.push_back(full_grid[static_cast<std::size_t>(index)]);
        }
    }
    return points;
}

PointList make151PointGrid() {
    const PointList full_grid = makeGrid(12, 13, 280.0, 180.0, 60.0, 58.0);
    const std::vector<int> omitted_indices{0, 12, 77, 143, 155};
    PointList points;
    points.reserve(151);
    for (int index = 0; index < static_cast<int>(full_grid.size()); ++index) {
        if (std::find(omitted_indices.begin(), omitted_indices.end(), index) == omitted_indices.end()) {
            points.push_back(full_grid[static_cast<std::size_t>(index)]);
        }
    }
    return points;
}

PointList translated(const PointList& source, const cv::Point2d& offset) {
    PointList result;
    result.reserve(source.size());
    for (const cv::Point2d& point : source) {
        result.push_back(point + offset);
    }
    return result;
}

PointList locallyDeformed94(const PointList& source) {
    const cv::Point2d pivot(640.0, 512.0);
    PointList result;
    result.reserve(source.size());
    for (const cv::Point2d& point : source) {
        const cv::Point2d local = point - pivot;
        const double dx = 8.0 + 0.012 * local.x + 5.0 * std::sin(local.y / 145.0);
        const double dy = -6.0 - 0.010 * local.y + 4.0 * std::cos(local.x / 170.0);
        result.emplace_back(point.x + dx, point.y + dy);
    }
    return result;
}

PointList prescriptionDeformed94(const PointList& source) {
    // S=-2.00 D, C=-1.00 D, A=45 degrees at distance_m=0.03.
    // M3 uses transform = I - distance_m * power_matrix.
    constexpr double xx = 1.075;
    constexpr double xy = -0.015;
    constexpr double yy = 1.075;
    const cv::Point2d pivot(640.0, 512.0);
    const cv::Point2d translation(8.0, -6.0);
    PointList result;
    result.reserve(source.size());
    for (const cv::Point2d& point : source) {
        const cv::Point2d local = point - pivot;
        result.emplace_back(
            pivot.x + translation.x + xx * local.x + xy * local.y,
            pivot.y + translation.y + xy * local.x + yy * local.y);
    }
    return result;
}

cv::Mat render(const PointList& points, const RenderOptions& options) {
    cv::Mat image(kImageHeight, kImageWidth, CV_32FC1);

    const auto deterministicNoise = [&options](const int row, const int column) {
        if (options.noise_half_amplitude <= 0.0) {
            return 0.0;
        }
        std::uint64_t value = options.noise_seed;
        value ^= static_cast<std::uint64_t>(row + 1) * 0x9E3779B185EBCA87ULL;
        value ^= static_cast<std::uint64_t>(column + 1) * 0xC2B2AE3D27D4EB4FULL;
        value ^= value >> 33U;
        value *= 0xFF51AFD7ED558CCDULL;
        value ^= value >> 33U;
        const double unit = static_cast<double>(value & 0xFFFFU) / 32767.5 - 1.0;
        return unit * options.noise_half_amplitude;
    };

    for (int row = 0; row < image.rows; ++row) {
        float* destination = image.ptr<float>(row);
        const double vertical = options.gradient_y * static_cast<double>(row) / static_cast<double>(kImageHeight - 1);
        for (int column = 0; column < image.cols; ++column) {
            const double horizontal = options.gradient_x * static_cast<double>(column) / static_cast<double>(kImageWidth - 1);
            destination[column] = static_cast<float>(
                options.background + horizontal + vertical + deterministicNoise(row, column));
        }
    }

    const int radius = std::max(1, static_cast<int>(std::ceil(options.spot_sigma * 4.0)));
    const double denominator = 2.0 * options.spot_sigma * options.spot_sigma;
    for (const cv::Point2d& point : points) {
        const int left = std::max(0, static_cast<int>(std::floor(point.x)) - radius);
        const int right = std::min(kImageWidth - 1, static_cast<int>(std::ceil(point.x)) + radius);
        const int top = std::max(0, static_cast<int>(std::floor(point.y)) - radius);
        const int bottom = std::min(kImageHeight - 1, static_cast<int>(std::ceil(point.y)) + radius);
        for (int row = top; row <= bottom; ++row) {
            float* destination = image.ptr<float>(row);
            for (int column = left; column <= right; ++column) {
                const double dx = static_cast<double>(column) - point.x;
                const double dy = static_cast<double>(row) - point.y;
                destination[column] += static_cast<float>(options.spot_amplitude * std::exp(-(dx * dx + dy * dy) / denominator));
            }
        }
    }

    cv::Mat result;
    image.convertTo(result, CV_8UC1);
    return result;
}

cv::Mat renderUndersizedComponents(const PointList& points) {
    cv::Mat image(kImageHeight, kImageWidth, CV_8UC1, cv::Scalar(12));
    for (const cv::Point2d& point : points) {
        const int center_x = cvRound(point.x);
        const int center_y = cvRound(point.y);
        cv::rectangle(
            image,
            cv::Rect(center_x - 1, center_y - 1, 3, 3),
            cv::Scalar(222),
            cv::FILLED);
    }
    return image;
}

bool writePng(const std::filesystem::path& path, const cv::Mat& image) {
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) {
        std::cerr << "Cannot create directory for " << path.generic_string() << ": " << error.message() << "\n";
        return false;
    }
    std::vector<unsigned char> encoded;
    if (!cv::imencode(".png", image, encoded)) {
        std::cerr << "Cannot encode PNG: " << path.generic_string() << "\n";
        return false;
    }
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        std::cerr << "Cannot open PNG for writing: " << path.generic_string() << "\n";
        return false;
    }
    output.write(reinterpret_cast<const char*>(encoded.data()), static_cast<std::streamsize>(encoded.size()));
    return output.good();
}

bool writeJson(const std::filesystem::path& path, const Json& content) {
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) {
        std::cerr << "Cannot create directory for " << path.generic_string() << ": " << error.message() << "\n";
        return false;
    }
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        std::cerr << "Cannot open JSON for writing: " << path.generic_string() << "\n";
        return false;
    }
    output << content.dump(2) << '\n';
    return output.good();
}

std::string projectRelativePath(const std::string& dataset_relative_path) {
    return std::string(kDatasetProjectPath) + "/" + dataset_relative_path;
}

Json knownCentersJson(const PointList& points) {
    Json centers = Json::array();
    for (std::size_t index = 0; index < points.size(); ++index) {
        centers.push_back({
            {"synthetic_point_id", static_cast<int>(index)},
            {"x", points[index].x},
            {"y", points[index].y},
        });
    }
    return centers;
}

Json makeInputPackage(const CaseSpec& spec) {
    return {
        {"schema_version", "1.0"},
        {"task_id", "m2_multispot_" + spec.id},
        {"module", "m1_input_config"},
        {"status", "ok"},
        {"data_source", "synthetic"},
        {"data", {
            {"calibration_image", projectRelativePath(spec.calibration_file)},
            {"measurement_image", projectRelativePath(spec.measurement_file)},
            {"config_path", "config/default_config.json"},
            {"run_mode", "local_image"},
            {"created_at", "2026-07-27T00:00:00+08:00"},
        }},
        {"quality", {
            {"paths_checked", true},
            {"config_checked", true},
            {"is_usable", true},
            {"warnings", Json::array({"SYNTHETIC_MULTISPOT_EXPERIMENTAL", "NOT_METROLOGY_VALIDATED"})},
        }},
        {"error", nullptr},
    };
}

Json caseManifestJson(const CaseSpec& spec) {
    Json tags = Json::array();
    if (spec.id.find("25") != std::string::npos) {
        tags.push_back("grid_25");
    }
    if (spec.id.find("94") != std::string::npos) {
        tags.push_back("grid_94");
    }
    if (spec.id.find("clean") != std::string::npos) {
        tags.push_back("clean");
    }
    if (spec.id.find("shift") != std::string::npos) {
        tags.push_back("shifted");
    }
    if (spec.id.find("deformation") != std::string::npos) {
        tags.push_back("deformed");
    }
    if (spec.id.find("prescription") != std::string::npos) {
        tags.push_back("known_prescription");
        tags.push_back("m2_m3_e2e");
    }
    if (spec.id.find("noisy") != std::string::npos) {
        tags.push_back("noisy");
        tags.push_back("gradient");
        tags.push_back("deformed");
    }
    if (spec.id.find("low_contrast") != std::string::npos) {
        tags.push_back("low_contrast");
        tags.push_back("shifted");
    }
    if (spec.id.find("brightness") != std::string::npos) {
        tags.push_back("brightness");
        tags.push_back("shifted");
    }
    if (spec.id.find("12bit") != std::string::npos) {
        tags.push_back("16bit_container");
    }
    if (spec.id.find("tiny") != std::string::npos) {
        tags.push_back("abnormal_area");
    }
    for (const auto& tag : std::vector<std::pair<std::string, std::string>>{
             {"missing", "missing"}, {"extra", "extra"}, {"merged", "merged"},
             {"edge", "edge"}, {"blank", "blank"}}) {
        if (spec.id.find(tag.first) != std::string::npos) {
            tags.push_back(tag.second);
        }
    }

    Json result{
        {"id", spec.id},
        {"outcome", spec.expected.status == "ok" ? "success" : "failure"},
        {"tags", std::move(tags)},
        {"input_package", "packages/" + spec.package_file},
        {"images", {
            {"calibration", spec.calibration_file},
            {"measurement", spec.measurement_file},
        }},
        {"expected_detection", {
            {"detector_mode", "hartmann_multispot_experimental"},
            {"status", spec.expected.status},
            {"min_spot_count", 12},
            {"max_spot_count", 150},
            {"expected_calibration_count", spec.expected_calibration_count},
            {"expected_measurement_count", spec.expected_measurement_count},
            {"expected_warnings", spec.expected.warnings},
            {"expected_error_code", spec.expected.error_code.empty() ? Json(nullptr) : Json(spec.expected.error_code)},
            {"input_white_level", spec.input_white_level > 0 ? Json(spec.input_white_level) : Json(nullptr)},
        }},
        {"transform", spec.transform},
        {"ground_truth", {
            {"calibration_known_centers", knownCentersJson(spec.calibration_centers)},
            {"measurement_known_centers", knownCentersJson(spec.measurement_centers)},
        }},
    };
    if (spec.expected.status == "ok") {
        const double max_rms_error = spec.id.find("noisy") != std::string::npos ||
                spec.id.find("low_contrast") != std::string::npos
            ? 1.0
            : 0.6;
        const double max_error = spec.id.find("noisy") != std::string::npos ||
                spec.id.find("low_contrast") != std::string::npos
            ? 2.0
            : 1.0;
        result["expected"] = {
            {"calibration", {
                {"expected_count", spec.expected_calibration_count},
                {"known_centers", knownCentersJson(spec.calibration_centers)},
                {"max_rms_error_px", max_rms_error},
                {"max_error_px", max_error},
            }},
            {"measurement", {
                {"expected_count", spec.expected_measurement_count},
                {"known_centers", knownCentersJson(spec.measurement_centers)},
                {"max_rms_error_px", max_rms_error},
                {"max_error_px", max_error},
            }},
        };
    }
    return result;
}

bool writeCasePackage(const std::filesystem::path& output, const CaseSpec& spec) {
    return writeJson(output / "packages" / spec.package_file, makeInputPackage(spec));
}

void addCase(Json& cases, const CaseSpec& spec) {
    cases.push_back(caseManifestJson(spec));
}

bool parseArguments(const std::vector<std::filesystem::path>& arguments, std::filesystem::path& output) {
    for (std::size_t index = 1; index < arguments.size(); ++index) {
        const std::string argument = arguments[index].generic_string();
        if (argument == "--output" && index + 1 < arguments.size()) {
            output = arguments[++index];
        } else if (argument == "--help") {
            std::cout << "Usage: m2_generate_multispot_synthetic --output <dataset_root>\n"
                      << "\n"
                      << "For packages/*.json to be directly usable with --project-root focimeter_system,\n"
                      << "use focimeter_system/data/mock/m2_image_recognition/synthetic_multispot as dataset_root.\n";
            return false;
        } else {
            std::cerr << "Unknown or incomplete argument: " << argument << "\n";
            return false;
        }
    }
    if (output.empty()) {
        std::cerr << "--output is required.\n";
        return false;
    }
    return true;
}

}  // namespace

int runGenerator(const std::vector<std::filesystem::path>& arguments) {
    std::filesystem::path output;
    if (!parseArguments(arguments, output)) {
        return arguments.size() == 2 && arguments[1].generic_string() == "--help" ? 0 : 2;
    }

    const PointList points25 = make25PointGrid();
    const PointList points94 = make94PointGrid();
    const PointList points151 = make151PointGrid();
    const PointList points25_shifted = translated(points25, {18.0, -13.0});
    const PointList points94_deformed = locallyDeformed94(points94);
    const PointList points94_prescription = prescriptionDeformed94(points94);

    cv::Mat clean25_12bit;
    cv::Mat shifted25_12bit;
    render(points25, {}).convertTo(clean25_12bit, CV_16U, 4095.0 / 255.0);
    render(points25_shifted, {}).convertTo(shifted25_12bit, CV_16U, 4095.0 / 255.0);

    PointList points11(points25.begin(), points25.begin() + 11);
    PointList points25_merged = points25;
    points25_merged[12] = {634.0, 512.0};
    points25_merged[13] = {648.0, 512.0};
    PointList points25_edge = points25;
    points25_edge[0] = {3.0, 512.0};
    const PointList points24_edge_filtered(points25_edge.begin() + 1, points25_edge.end());

    bool ok = true;
    ok = writePng(output / "calibration/25_clean_reference.png", render(points25, {})) && ok;
    ok = writePng(output / "measurement/25_measured_shift.png", render(points25_shifted, {})) && ok;
    ok = writePng(output / "calibration/25_clean_reference_12bit.png", clean25_12bit) && ok;
    ok = writePng(output / "measurement/25_measured_shift_12bit.png", shifted25_12bit) && ok;
    ok = writePng(output / "calibration/94_clean_reference.png", render(points94, {})) && ok;
    ok = writePng(output / "measurement/94_measured_local_deformation.png", render(points94_deformed, {})) && ok;
    ok = writePng(output / "measurement/94_measured_prescription_s-2_c-1_a45.png", render(points94_prescription, {})) && ok;
    ok = writePng(
             output / "measurement/94_measured_noisy_gradient.png",
             render(points94_deformed, {24.0, 20.0, 34.0, 5.5, 190.0, 6.5, kSeed + 94U})) &&
         ok;
    ok = writePng(
             output / "measurement/25_measured_low_contrast.png",
             render(points25_shifted, {100.0, 4.0, 7.0, 2.5, 44.0, 6.0, kSeed + 25U})) &&
         ok;
    ok = writePng(
             output / "measurement/25_measured_brightness.png",
             render(points25_shifted, {30.0, 0.0, 0.0, 1.5, 120.0, 6.0, kSeed + 26U})) &&
         ok;
    ok = writePng(output / "failure/missing_11.png", render(points11, {})) && ok;
    ok = writePng(output / "failure/extra_151.png", render(points151, {12.0, 0.0, 0.0, 1.0, 205.0, 5.0, kSeed + 151U})) && ok;
    ok = writePng(output / "failure/merged_25.png", render(points25_merged, {12.0, 0.0, 0.0, 0.5, 210.0, 7.5, kSeed + 225U})) && ok;
    ok = writePng(output / "failure/edge_clipped_25.png", render(points25_edge, {})) && ok;
    ok = writePng(output / "failure/tiny_25.png", renderUndersizedComponents(points25)) && ok;
    ok = writePng(output / "failure/blank_dark.png", cv::Mat::zeros(kImageHeight, kImageWidth, CV_8UC1)) && ok;
    ok = writePng(output / "failure/blank_bright.png", cv::Mat(kImageHeight, kImageWidth, CV_8UC1, cv::Scalar(255))) && ok;

    Json cases = Json::array();
    const ExpectedOutcome expected_ok{"ok", "", {}};
    const CaseSpec clean25{
        "25_clean_shift", "input_package_25_clean_shift.json", "calibration/25_clean_reference.png",
        "measurement/25_measured_shift.png", points25, points25_shifted, 25, 25,
        {{"kind", "global_translation"}, {"translation_pixel", {18.0, -13.0}}}, expected_ok};
    const CaseSpec clean25_12bit_case{
        "25_12bit_shift", "input_package_25_12bit_shift.json", "calibration/25_clean_reference_12bit.png",
        "measurement/25_measured_shift_12bit.png", points25, points25_shifted, 25, 25,
        {{"kind", "global_translation_12bit_in_16bit_container"}, {"translation_pixel", {18.0, -13.0}},
         {"input_white_level", 4095}}, expected_ok, 4095};
    const CaseSpec clean94{
        "94_clean_local_deformation", "input_package_94_clean_local_deformation.json", "calibration/94_clean_reference.png",
        "measurement/94_measured_local_deformation.png", points94, points94_deformed, 94, 94,
        {{"kind", "translation_plus_smooth_local_deformation"}, {"base_translation_pixel", {8.0, -6.0}},
         {"deformation", "dx=0.012*x_local+5*sin(y_local/145); dy=-0.010*y_local+4*cos(x_local/170)"}}, expected_ok};
    const CaseSpec prescription94{
        "94_known_prescription", "input_package_94_known_prescription.json", "calibration/94_clean_reference.png",
        "measurement/94_measured_prescription_s-2_c-1_a45.png", points94, points94_prescription, 94, 94,
        {{"kind", "m3_power_matrix_simulation"}, {"distance_m", 0.03},
         {"prescription", {{"S", -2.0}, {"C", -1.0}, {"A", 45.0}, {"unit", "D"}}},
         {"matrix", {{1.075, -0.015}, {-0.015, 1.075}}}, {"translation_pixel", {8.0, -6.0}}}, expected_ok};
    const CaseSpec noisy94{
        "94_noisy_gradient", "input_package_94_noisy_gradient.json", "calibration/94_clean_reference.png",
        "measurement/94_measured_noisy_gradient.png", points94, points94_deformed, 94, 94,
        {{"kind", "same_local_deformation_with_illumination_degradation"}, {"gradient_x_8bit", 20.0},
         {"gradient_y_8bit", 34.0}, {"noise_half_amplitude_8bit", 5.5}},
        {"ok", "", {"SATURATED_PEAK"}}};
    const CaseSpec lowContrast25{
        "25_low_contrast", "input_package_25_low_contrast.json", "calibration/25_clean_reference.png",
        "measurement/25_measured_low_contrast.png", points25, points25_shifted, 25, 25,
        {{"kind", "global_translation_low_contrast"}, {"translation_pixel", {18.0, -13.0}},
         {"background_8bit", 100.0}, {"spot_amplitude_8bit", 44.0}, {"noise_half_amplitude_8bit", 2.5}},
        {"ok", "", {}}};
    const CaseSpec brightness25{
        "25_brightness", "input_package_25_brightness.json", "calibration/25_clean_reference.png",
        "measurement/25_measured_brightness.png", points25, points25_shifted, 25, 25,
        {{"kind", "global_translation_brightness_change"}, {"translation_pixel", {18.0, -13.0}},
         {"background_8bit", 30.0}, {"spot_amplitude_8bit", 120.0}, {"noise_half_amplitude_8bit", 1.5}},
        expected_ok};
    const CaseSpec missing{
        "missing_11", "input_package_missing_11.json", "calibration/25_clean_reference.png", "failure/missing_11.png",
        points25, points11, 25, 11, {{"kind", "missing_spots"}, {"removed_count", 14}},
        {"error", "SPOT_COUNT_MISMATCH", {}}};
    const CaseSpec extra{
        "extra_151", "input_package_extra_151.json", "calibration/25_clean_reference.png", "failure/extra_151.png",
        points25, points151, 25, 151, {{"kind", "candidate_count_above_maximum"}, {"max_spot_count", 150}},
        {"error", "SPOT_COUNT_MISMATCH", {"SPOT_LIMIT_EXCEEDED"}}};
    const CaseSpec merged{
        "merged_25", "input_package_merged_25.json", "calibration/25_clean_reference.png", "failure/merged_25.png",
        points25, points25_merged, 25, 24,
        {{"kind", "two_nearby_gaussian_spots"}, {"nearest_center_distance_pixel", 14.0}},
        {"error", "CENTROID_FAILED", {"POSSIBLE_MERGED_COMPONENT", "SATURATED_PEAK"}}};
    const CaseSpec edgeClipped{
        "edge_clipped_25", "input_package_edge_clipped_25.json", "calibration/25_clean_reference.png", "failure/edge_clipped_25.png",
        points25, points24_edge_filtered, 25, 24,
        {{"kind", "spot_center_near_image_edge"}, {"edge_center_x_pixel", 3.0}},
        {"ok", "", {"EDGE_CLIPPED_CANDIDATE_REJECTED"}}};
    const CaseSpec tiny{
        "tiny_25", "input_package_tiny_25.json", "calibration/25_clean_reference.png", "failure/tiny_25.png",
        points25, points25, 25, 0,
        {{"kind", "all_components_below_minimum_area"}, {"component_shape", "3x3_filled_square"},
         {"minimum_area_pixel", 12}},
        {"error", "SPOT_COUNT_MISMATCH", {}}};
    const CaseSpec blankDark{
        "blank_dark", "input_package_blank_dark.json", "calibration/25_clean_reference.png", "failure/blank_dark.png",
        points25, {}, 25, 0, {{"kind", "uniform_dark_frame"}},
        {"error", "SPOT_COUNT_MISMATCH", {"IMAGE_UNDEREXPOSED", "IMAGE_LOW_CONTRAST"}}};
    const CaseSpec blankBright{
        "blank_bright", "input_package_blank_bright.json", "calibration/25_clean_reference.png", "failure/blank_bright.png",
        points25, {}, 25, 0, {{"kind", "uniform_saturated_frame"}},
        {"error", "SPOT_COUNT_MISMATCH", {"IMAGE_OVEREXPOSED", "IMAGE_LOW_CONTRAST"}}};

    const std::vector<CaseSpec> specifications{
        clean25, clean25_12bit_case, clean94, prescription94, noisy94, lowContrast25, brightness25,
        missing, extra, merged, edgeClipped, tiny, blankDark, blankBright};
    for (const CaseSpec& specification : specifications) {
        ok = writeCasePackage(output, specification) && ok;
        addCase(cases, specification);
    }

    // This manifest lives under the repository-wide mock directory, whose validator
    // requires the common v1 envelope even for dataset metadata.
    const Json manifest{
        {"schema_version", "1.0"},
        {"task_id", "m2_multispot_synthetic_dataset"},
        {"module", "m2_image_recognition"},
        {"status", "ok"},
        {"generator_version", kGeneratorVersion},
        {"seed", kSeed},
        {"data_source", "synthetic"},
        {"validation_status", "software_verified"},
        {"metrology_validated", false},
        {"image", {{"width", kImageWidth}, {"height", kImageHeight}, {"bit_depths", Json::array({8, 16})}, {"format", "png"}}},
        {"detector", {{"min_spot_count", 12}, {"max_spot_count", 150}}},
        {"coordinate_system", {{"coordinate_type", "image_pixel"}, {"origin", "top_left"}, {"x_positive", "right"}, {"y_positive", "down"}}},
        {"identity_contract", {{"physical_identity_guaranteed", false}, {"reason", "This dataset validates detection only; cross-image physical-ray matching is intentionally outside this generator."}}},
        {"important_limitations", Json::array({
            "All points, transformations, noise and backgrounds are generated fixtures, not LM700 hardware captures.",
            "synthetic_point_id is local manifest bookkeeping only and is not a formal M2 spot_id or physical-ray identity.",
            "Expected error categories are test expectations, not additions to the shared v1 error-code contract.",
        })},
        {"cases", cases},
        {"error", nullptr},
    };
    ok = writeJson(output / "manifest.json", manifest) && ok;

    if (!ok) {
        std::cerr << "Multispot synthetic data generation failed.\n";
        return 1;
    }
    std::cout << "Multispot synthetic data written to " << output.generic_string() << "\n";
    return 0;
}

#ifdef _WIN32
int wmain(int argc, wchar_t* argv[]) {
    std::vector<std::filesystem::path> arguments;
    arguments.reserve(static_cast<std::size_t>(argc));
    for (int index = 0; index < argc; ++index) {
        arguments.emplace_back(argv[index]);
    }
    return runGenerator(arguments);
}
#else
int main(int argc, char* argv[]) {
    std::vector<std::filesystem::path> arguments;
    arguments.reserve(static_cast<std::size_t>(argc));
    for (int index = 0; index < argc; ++index) {
        arguments.emplace_back(std::filesystem::u8path(argv[index]));
    }
    return runGenerator(arguments);
}
#endif
