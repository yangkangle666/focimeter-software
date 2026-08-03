#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>
#include <opencv2/imgcodecs.hpp>

#include "focimeter/m2/image_processor.h"
#include "focimeter/m2/json_io.h"
#include "focimeter/m2/module.h"

namespace {

using focimeter::m2::ImageAnalysis;
using focimeter::m2::ImageProcessor;
using focimeter::m2::ImageRecognitionModule;
using focimeter::m2::ProcessingConfig;
using focimeter::m2::RecognitionMode;
using focimeter::m2::RunOptions;
using focimeter::m2::readProcessingConfig;
using focimeter::m2::writeExperimentalMultispotSuccess;
using nlohmann::json;

struct TestContext {
    int failures{0};

    void expect(const bool condition, const std::string& message) {
        if (!condition) {
            ++failures;
            std::cerr << "FAIL: " << message << "\n";
        }
    }
};

struct ImageExpectation {
    int expected_count{0};
    std::vector<cv::Point2d> known_centers;
    double max_rms_error_px{0.0};
    double max_error_px{0.0};
};

struct FixtureCase {
    std::string id;
    std::set<std::string> tags;
    bool expect_success{false};
    std::string expected_error_code;
    std::vector<std::string> expected_warnings;
    int expected_calibration_count{0};
    int expected_measurement_count{0};
    std::optional<int> input_white_level;
    std::filesystem::path input_package;
    std::filesystem::path calibration_image;
    std::filesystem::path measurement_image;
    ImageExpectation calibration;
    ImageExpectation measurement;
};

struct DetectorLimits {
    int min_spot_count{0};
    int max_spot_count{0};
};

struct PointMatchMetrics {
    bool complete{false};
    double rms_error{std::numeric_limits<double>::infinity()};
    double max_error{std::numeric_limits<double>::infinity()};
};

std::optional<json> readJsonFile(
    TestContext& test,
    const std::filesystem::path& path,
    const std::string& label) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        test.expect(false, label + " must exist and be readable: " + path.generic_string());
        return std::nullopt;
    }

    try {
        return json::parse(input);
    } catch (const json::exception& exception) {
        test.expect(false, label + " must be valid JSON: " + exception.what());
        return std::nullopt;
    }
}

bool writeJsonFile(const std::filesystem::path& path, const json& document) {
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) {
        return false;
    }
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << document.dump(2) << '\n';
    return output.good();
}

bool writeTextFile(const std::filesystem::path& path, const std::string& text) {
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) {
        return false;
    }
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << text;
    return output.good();
}

bool writePngFile(const std::filesystem::path& path, const cv::Mat& image) {
    std::vector<unsigned char> bytes;
    if (!cv::imencode(".png", image, bytes)) {
        return false;
    }
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) {
        return false;
    }
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return output.good();
}

bool readStringField(
    TestContext& test,
    const json& object,
    const char* field,
    const std::string& context,
    std::string& value) {
    if (!object.is_object() || !object.contains(field) || !object.at(field).is_string()) {
        test.expect(false, context + "." + field + " must be a string");
        return false;
    }
    value = object.at(field).get<std::string>();
    return true;
}

bool readPositiveIntField(
    TestContext& test,
    const json& object,
    const char* field,
    const std::string& context,
    int& value) {
    if (!object.is_object() || !object.contains(field) || !object.at(field).is_number_integer()) {
        test.expect(false, context + "." + field + " must be an integer");
        return false;
    }
    value = object.at(field).get<int>();
    if (value <= 0) {
        test.expect(false, context + "." + field + " must be positive");
        return false;
    }
    return true;
}

bool readPositiveNumberField(
    TestContext& test,
    const json& object,
    const char* field,
    const std::string& context,
    double& value) {
    if (!object.is_object() || !object.contains(field) || !object.at(field).is_number()) {
        test.expect(false, context + "." + field + " must be numeric");
        return false;
    }
    value = object.at(field).get<double>();
    if (!std::isfinite(value) || value <= 0.0) {
        test.expect(false, context + "." + field + " must be finite and positive");
        return false;
    }
    return true;
}

std::optional<std::filesystem::path> readRelativePath(
    TestContext& test,
    const json& object,
    const char* field,
    const std::filesystem::path& root,
    const std::string& context) {
    std::string serialized_path;
    if (!readStringField(test, object, field, context, serialized_path)) {
        return std::nullopt;
    }

    const std::filesystem::path relative_path = std::filesystem::u8path(serialized_path);
    const std::filesystem::path normalized_path = relative_path.lexically_normal();
    bool traverses_outside_root = false;
    for (const auto& component : normalized_path) {
        if (component == "..") {
            traverses_outside_root = true;
            break;
        }
    }
    if (relative_path.empty() || relative_path.is_absolute() || traverses_outside_root) {
        test.expect(false, context + "." + field + " must be a relative path inside synthetic_root");
        return std::nullopt;
    }
    return root / relative_path;
}

std::optional<ImageExpectation> parseImageExpectation(
    TestContext& test,
    const json& document,
    const std::string& context) {
    ImageExpectation expectation;
    if (!readPositiveIntField(test, document, "expected_count", context, expectation.expected_count) ||
        !readPositiveNumberField(test, document, "max_rms_error_px", context, expectation.max_rms_error_px) ||
        !readPositiveNumberField(test, document, "max_error_px", context, expectation.max_error_px)) {
        return std::nullopt;
    }

    if (!document.contains("known_centers") || !document.at("known_centers").is_array()) {
        test.expect(false, context + ".known_centers must be an array");
        return std::nullopt;
    }
    for (std::size_t index = 0; index < document.at("known_centers").size(); ++index) {
        const auto& center = document.at("known_centers").at(index);
        const std::string center_context = context + ".known_centers[" + std::to_string(index) + "]";
        if (!center.is_object() || !center.contains("x") || !center.at("x").is_number() ||
            !center.contains("y") || !center.at("y").is_number()) {
            test.expect(false, center_context + " must contain numeric x and y");
            return std::nullopt;
        }
        const double x = center.at("x").get<double>();
        const double y = center.at("y").get<double>();
        if (!std::isfinite(x) || !std::isfinite(y)) {
            test.expect(false, center_context + " coordinates must be finite");
            return std::nullopt;
        }
        expectation.known_centers.emplace_back(x, y);
    }

    if (static_cast<int>(expectation.known_centers.size()) != expectation.expected_count) {
        test.expect(false, context + ".known_centers size must equal expected_count");
        return std::nullopt;
    }
    return expectation;
}

std::optional<std::pair<DetectorLimits, std::vector<FixtureCase>>> loadFixtures(
    TestContext& test,
    const std::filesystem::path& synthetic_root) {
    const auto manifest_document = readJsonFile(test, synthetic_root / "manifest.json", "multispot manifest");
    if (!manifest_document.has_value() || !manifest_document->is_object()) {
        return std::nullopt;
    }

    const json& manifest = *manifest_document;
    if (!manifest.contains("detector") || !manifest.at("detector").is_object()) {
        test.expect(false, "manifest.detector must describe the experimental count limits");
        return std::nullopt;
    }
    DetectorLimits limits;
    if (!readPositiveIntField(test, manifest.at("detector"), "min_spot_count", "manifest.detector", limits.min_spot_count) ||
        !readPositiveIntField(test, manifest.at("detector"), "max_spot_count", "manifest.detector", limits.max_spot_count) ||
        limits.min_spot_count > limits.max_spot_count) {
        test.expect(limits.min_spot_count <= limits.max_spot_count, "manifest detector limits must be ordered");
        return std::nullopt;
    }
    if (!manifest.contains("cases") || !manifest.at("cases").is_array() || manifest.at("cases").empty()) {
        test.expect(false, "manifest.cases must be a non-empty array");
        return std::nullopt;
    }

    std::vector<FixtureCase> fixtures;
    std::set<std::string> ids;
    for (std::size_t index = 0; index < manifest.at("cases").size(); ++index) {
        const auto& source = manifest.at("cases").at(index);
        const std::string context = "manifest.cases[" + std::to_string(index) + "]";
        if (!source.is_object()) {
            test.expect(false, context + " must be an object");
            continue;
        }

        FixtureCase fixture;
        std::string outcome;
        if (!readStringField(test, source, "id", context, fixture.id) ||
            !readStringField(test, source, "outcome", context, outcome)) {
            continue;
        }
        if (!ids.insert(fixture.id).second) {
            test.expect(false, context + ".id must be unique");
            continue;
        }
        if (outcome == "success") {
            fixture.expect_success = true;
        } else if (outcome == "failure") {
            fixture.expect_success = false;
        } else {
            test.expect(false, context + ".outcome must be success or failure");
            continue;
        }

        if (!source.contains("tags") || !source.at("tags").is_array() || source.at("tags").empty()) {
            test.expect(false, context + ".tags must be a non-empty string array");
            continue;
        }
        bool valid_tags = true;
        for (const auto& tag : source.at("tags")) {
            if (!tag.is_string()) {
                test.expect(false, context + ".tags must contain only strings");
                valid_tags = false;
                break;
            }
            fixture.tags.insert(tag.get<std::string>());
        }
        if (!valid_tags) {
            continue;
        }

        const auto package_path = readRelativePath(test, source, "input_package", synthetic_root, context);
        if (!package_path.has_value()) {
            continue;
        }
        fixture.input_package = *package_path;

        if (!source.contains("images") || !source.at("images").is_object()) {
            test.expect(false, context + ".images must be an object");
            continue;
        }
        const auto calibration_path = readRelativePath(
            test, source.at("images"), "calibration", synthetic_root, context + ".images");
        const auto measurement_path = readRelativePath(
            test, source.at("images"), "measurement", synthetic_root, context + ".images");
        if (!calibration_path.has_value() || !measurement_path.has_value()) {
            continue;
        }
        fixture.calibration_image = *calibration_path;
        fixture.measurement_image = *measurement_path;

        if (!source.contains("expected_detection") ||
            !source.at("expected_detection").is_object()) {
            test.expect(false, context + ".expected_detection must be an object");
            continue;
        }
        const auto& expected_detection = source.at("expected_detection");
        std::string detector_mode;
        std::string expected_status;
        if (!readStringField(
                test, expected_detection, "detector_mode", context + ".expected_detection", detector_mode) ||
            !readStringField(
                test, expected_detection, "status", context + ".expected_detection", expected_status) ||
            detector_mode != "hartmann_multispot_experimental" ||
            expected_status != (fixture.expect_success ? "ok" : "error")) {
            test.expect(
                detector_mode == "hartmann_multispot_experimental",
                context + ".expected_detection.detector_mode must identify the experimental detector");
            test.expect(
                expected_status == (fixture.expect_success ? "ok" : "error"),
                context + ".expected_detection.status must match outcome");
            continue;
        }
        const auto read_nonnegative_count = [&test, &expected_detection, &context](
                                                const char* field,
                                                int& destination) {
            if (!expected_detection.contains(field) ||
                !expected_detection.at(field).is_number_integer()) {
                test.expect(false, context + ".expected_detection." + field + " must be an integer");
                return false;
            }
            destination = expected_detection.at(field).get<int>();
            test.expect(destination >= 0, context + ".expected_detection." + field + " must be non-negative");
            return destination >= 0;
        };
        if (!read_nonnegative_count("expected_calibration_count", fixture.expected_calibration_count) ||
            !read_nonnegative_count("expected_measurement_count", fixture.expected_measurement_count)) {
            continue;
        }
        if (!expected_detection.contains("expected_warnings") ||
            !expected_detection.at("expected_warnings").is_array()) {
            test.expect(false, context + ".expected_detection.expected_warnings must be an array");
            continue;
        }
        bool warnings_valid = true;
        for (const auto& warning : expected_detection.at("expected_warnings")) {
            if (!warning.is_string()) {
                warnings_valid = false;
                break;
            }
            fixture.expected_warnings.push_back(warning.get<std::string>());
        }
        if (!warnings_valid) {
            test.expect(false, context + ".expected_detection.expected_warnings must contain strings");
            continue;
        }
        if (!expected_detection.contains("expected_error_code")) {
            test.expect(false, context + ".expected_detection.expected_error_code must be present");
            continue;
        }
        const auto& expected_error = expected_detection.at("expected_error_code");
        if (fixture.expect_success) {
            if (!expected_error.is_null()) {
                test.expect(false, context + ".expected_detection.expected_error_code must be null on success");
                continue;
            }
        } else if (!expected_error.is_string() || expected_error.get_ref<const std::string&>().empty()) {
            test.expect(false, context + ".expected_detection.expected_error_code must be a non-empty string on failure");
            continue;
        } else {
            fixture.expected_error_code = expected_error.get<std::string>();
        }
        if (!expected_detection.contains("input_white_level")) {
            test.expect(false, context + ".expected_detection.input_white_level must be present");
            continue;
        }
        const auto& white_level = expected_detection.at("input_white_level");
        if (white_level.is_number_integer() && white_level.get<int>() > 0) {
            fixture.input_white_level = white_level.get<int>();
        } else if (!white_level.is_null()) {
            test.expect(
                false,
                context + ".expected_detection.input_white_level must be null or a positive integer");
            continue;
        }

        if (fixture.expect_success) {
            if (!source.contains("expected") || !source.at("expected").is_object()) {
                test.expect(false, context + ".expected must be present for a successful case");
                continue;
            }
            const auto& expected = source.at("expected");
            if (!expected.contains("calibration") || !expected.at("calibration").is_object() ||
                !expected.contains("measurement") || !expected.at("measurement").is_object()) {
                test.expect(false, context + ".expected must contain calibration and measurement objects");
                continue;
            }
            const auto calibration = parseImageExpectation(
                test, expected.at("calibration"), context + ".expected.calibration");
            const auto measurement = parseImageExpectation(
                test, expected.at("measurement"), context + ".expected.measurement");
            if (!calibration.has_value() || !measurement.has_value()) {
                continue;
            }
            fixture.calibration = *calibration;
            fixture.measurement = *measurement;
        }

        fixtures.push_back(std::move(fixture));
    }
    return std::make_pair(limits, fixtures);
}

PointMatchMetrics matchKnownCenters(
    const std::vector<cv::Point2d>& detected,
    const std::vector<cv::Point2d>& known) {
    if (detected.size() != known.size()) {
        return {};
    }

    struct Candidate {
        double distance;
        std::size_t detected_index;
        std::size_t known_index;
    };
    std::vector<Candidate> candidates;
    candidates.reserve(detected.size() * known.size());
    for (std::size_t detected_index = 0; detected_index < detected.size(); ++detected_index) {
        for (std::size_t known_index = 0; known_index < known.size(); ++known_index) {
            candidates.push_back({
                cv::norm(detected[detected_index] - known[known_index]), detected_index, known_index});
        }
    }
    std::sort(candidates.begin(), candidates.end(), [](const Candidate& left, const Candidate& right) {
        return left.distance < right.distance;
    });

    std::vector<bool> detected_used(detected.size(), false);
    std::vector<bool> known_used(known.size(), false);
    std::vector<double> distances;
    distances.reserve(detected.size());
    for (const auto& candidate : candidates) {
        if (detected_used[candidate.detected_index] || known_used[candidate.known_index]) {
            continue;
        }
        detected_used[candidate.detected_index] = true;
        known_used[candidate.known_index] = true;
        distances.push_back(candidate.distance);
    }
    if (distances.size() != known.size()) {
        return {};
    }

    double sum_squared_error = 0.0;
    double max_error = 0.0;
    for (const double distance : distances) {
        sum_squared_error += distance * distance;
        max_error = std::max(max_error, distance);
    }
    return {
        true,
        std::sqrt(sum_squared_error / static_cast<double>(distances.size())),
        max_error};
}

void expectPointAccuracy(
    TestContext& test,
    const std::vector<cv::Point2d>& actual,
    const ImageExpectation& expected,
    const std::string& context) {
    test.expect(
        static_cast<int>(actual.size()) == expected.expected_count,
        context + " must contain exactly the expected number of detected points");
    if (static_cast<int>(actual.size()) != expected.expected_count) {
        return;
    }

    const PointMatchMetrics metrics = matchKnownCenters(actual, expected.known_centers);
    test.expect(metrics.complete, context + " points must support one-to-one ground-truth matching");
    if (!metrics.complete) {
        return;
    }
    test.expect(
        metrics.rms_error <= expected.max_rms_error_px,
        context + " RMS coordinate error must stay within the synthetic acceptance bound");
    test.expect(
        metrics.max_error <= expected.max_error_px,
        context + " maximum coordinate error must stay within the synthetic acceptance bound");
}

void expectObservationQuality(
    TestContext& test,
    const ImageAnalysis& analysis,
    const std::string& context) {
    for (std::size_t index = 0; index < analysis.observations.size(); ++index) {
        const auto& observation = analysis.observations[index];
        const std::string point_context = context + " observation " + std::to_string(index);
        test.expect(
            std::isfinite(observation.center.x) && std::isfinite(observation.center.y),
            point_context + " center must be finite");
        test.expect(
            std::isfinite(observation.confidence) && observation.confidence >= 0.0 && observation.confidence <= 1.0,
            point_context + " confidence must be within [0, 1]");
        test.expect(
            std::isfinite(observation.area) && observation.area > 0.0 &&
                observation.area <= static_cast<double>(analysis.gray.total()),
            point_context + " area must be finite and positive");
        test.expect(
            std::isfinite(observation.mean_intensity) && observation.mean_intensity > 0.0 &&
                observation.mean_intensity <= 255.0,
            point_context + " mean intensity must be within the 8-bit domain");
        test.expect(
            std::isfinite(observation.peak_intensity) &&
                observation.peak_intensity >= observation.mean_intensity &&
                observation.peak_intensity <= 255.0,
            point_context + " peak intensity must be within the 8-bit domain and not below the mean");
        test.expect(
            std::isfinite(observation.peak_residual_intensity) &&
                observation.peak_residual_intensity > 0.0 &&
                observation.peak_residual_intensity <= 255.0,
            point_context + " residual peak must be within the 8-bit residual domain");
        test.expect(
            std::isfinite(observation.integrated_intensity) &&
                observation.integrated_intensity > 0.0 &&
                observation.integrated_intensity <= observation.area * 255.0,
            point_context + " integrated residual must be bounded by area times 255");
    }
}

std::optional<std::vector<cv::Point2d>> expectExperimentalSuccessDocument(
    TestContext& test,
    const json& document,
    const std::string& task_id,
    const std::string& image_type,
    const ImageExpectation& expected) {
    const std::string context = image_type + " experimental output";
    test.expect(document.is_object(), context + " must be a JSON object");
    if (!document.is_object()) {
        return std::nullopt;
    }

    const auto expect_string = [&test, &document, &context](const char* field, const char* value) {
        test.expect(
            document.contains(field) && document.at(field).is_string() && document.at(field) == value,
            context + "." + field + " must equal " + value);
    };
    expect_string("module", "m2_image_recognition");
    expect_string("status", "ok");
    expect_string("image_type", image_type.c_str());
    expect_string("coordinate_type", "image_pixel");
    expect_string("contract_status", "proposed");
    expect_string("data_source", "synthetic");
    expect_string("validation_status", "software_verified");
    expect_string("validation_scope", "simulation_only");
    test.expect(
        document.contains("schema_version") && document.at("schema_version").is_string() &&
            document.at("schema_version") == "m2.multispot.experimental.1",
        context + ".schema_version must equal the isolated experimental version");
    test.expect(
        document.contains("task_id") && document.at("task_id").is_string() && document.at("task_id") == task_id,
        context + ".task_id must retain the input task id");
    test.expect(
        document.contains("experimental") && document.at("experimental").is_boolean() && document.at("experimental").get<bool>(),
        context + ".experimental must be true");
    test.expect(
        document.contains("metrology_validated") && document.at("metrology_validated").is_boolean() &&
            !document.at("metrology_validated").get<bool>(),
        context + ".metrology_validated must be false");
    test.expect(
        document.contains("error") && document.at("error").is_null(),
        context + ".error must be null on success");

    test.expect(document.contains("matching") && document.at("matching").is_object(), context + ".matching must be an object");
    if (document.contains("matching") && document.at("matching").is_object()) {
        const auto& matching = document.at("matching");
        test.expect(
            matching.contains("status") && matching.at("status") == "not_performed",
            context + ".matching.status must be not_performed");
        test.expect(
            matching.contains("id_scope") && matching.at("id_scope") == "image_local",
            context + ".matching.id_scope must be image_local");
        test.expect(
            matching.contains("physical_identity_guaranteed") && matching.at("physical_identity_guaranteed").is_boolean() &&
                !matching.at("physical_identity_guaranteed").get<bool>(),
            context + ".matching.physical_identity_guaranteed must be false");
        test.expect(
            matching.contains("owner_status") && matching.at("owner_status") == "unassigned",
            context + ".matching.owner_status must remain unassigned until the public contract is approved");
    }

    test.expect(document.contains("quality") && document.at("quality").is_object(), context + ".quality must be an object");
    if (document.contains("quality") && document.at("quality").is_object()) {
        const auto& quality = document.at("quality");
        test.expect(
            quality.contains("min_count") && quality.at("min_count").is_number_integer(),
            context + ".quality.min_count must be an integer");
        test.expect(
            quality.contains("max_count") && quality.at("max_count").is_number_integer(),
            context + ".quality.max_count must be an integer");
        test.expect(
            quality.contains("detected_count") && quality.at("detected_count").is_number_integer() &&
                quality.at("detected_count") == expected.expected_count,
            context + ".quality.detected_count must equal the manifest ground-truth count");
        test.expect(
            quality.contains("is_usable") && quality.at("is_usable").is_boolean() && quality.at("is_usable").get<bool>(),
            context + ".quality.is_usable must be true");
    }

    if (!document.contains("spots") || !document.at("spots").is_array()) {
        test.expect(false, context + ".spots must be an array");
        return std::nullopt;
    }
    const auto& spots = document.at("spots");
    test.expect(
        static_cast<int>(spots.size()) == expected.expected_count,
        context + ".spots count must equal manifest ground truth");

    std::set<int> detection_ids;
    std::vector<cv::Point2d> points;
    points.reserve(spots.size());
    for (std::size_t index = 0; index < spots.size(); ++index) {
        const auto& spot = spots.at(index);
        const std::string spot_context = context + ".spots[" + std::to_string(index) + "]";
        const bool shape_ok = spot.is_object() &&
            spot.contains("detection_id") && spot.at("detection_id").is_number_integer() &&
            spot.contains("x") && spot.at("x").is_number() &&
            spot.contains("y") && spot.at("y").is_number() &&
            spot.contains("confidence") && spot.at("confidence").is_number() &&
            spot.contains("area_pixel2") && spot.at("area_pixel2").is_number() &&
            spot.contains("mean_intensity_8bit") && spot.at("mean_intensity_8bit").is_number() &&
            spot.contains("peak_intensity_8bit") && spot.at("peak_intensity_8bit").is_number() &&
            spot.contains("peak_residual_intensity_8bit") && spot.at("peak_residual_intensity_8bit").is_number() &&
            spot.contains("integrated_residual_8bit_pixel") && spot.at("integrated_residual_8bit_pixel").is_number() &&
            spot.contains("quality_flags") && spot.at("quality_flags").is_array();
        test.expect(shape_ok, spot_context + " must have the experimental detection fields");
        test.expect(!spot.contains("spot_id"), spot_context + " must not reuse v1 physical spot_id");
        test.expect(!spot.contains("role"), spot_context + " must not reuse v1 five-spot role");
        if (!shape_ok) {
            continue;
        }

        const int detection_id = spot.at("detection_id").get<int>();
        const double x = spot.at("x").get<double>();
        const double y = spot.at("y").get<double>();
        const double confidence = spot.at("confidence").get<double>();
        const double area = spot.at("area_pixel2").get<double>();
        const double mean_intensity = spot.at("mean_intensity_8bit").get<double>();
        const double peak_residual = spot.at("peak_residual_intensity_8bit").get<double>();
        const double peak_intensity = spot.at("peak_intensity_8bit").get<double>();
        const double integrated_intensity = spot.at("integrated_residual_8bit_pixel").get<double>();
        test.expect(
            detection_id == static_cast<int>(index) && detection_ids.insert(detection_id).second,
            spot_context + " detection_id must be continuous, unique, and equal to its sorted index");
        test.expect(std::isfinite(x) && std::isfinite(y), spot_context + " coordinates must be finite");
        test.expect(
            std::isfinite(confidence) && confidence >= 0.0 && confidence <= 1.0,
            spot_context + " confidence must be within [0, 1]");
        test.expect(std::isfinite(area) && area > 0.0, spot_context + " area must be positive");
        test.expect(std::isfinite(mean_intensity) && mean_intensity > 0.0, spot_context + " mean_intensity must be positive");
        test.expect(
            std::isfinite(peak_intensity) && peak_intensity >= mean_intensity && peak_intensity <= 255.0,
            spot_context + " raw peak must be within [mean, 255]");
        test.expect(std::isfinite(peak_residual) && peak_residual > 0.0, spot_context + " residual peak must be positive");
        test.expect(
            std::isfinite(integrated_intensity) && integrated_intensity > 0.0,
            spot_context + " integrated_intensity must be positive");
        points.emplace_back(x, y);
    }
    return points;
}

void expectExperimentalFailureDocument(
    TestContext& test,
    const json& document,
    const std::string& task_id,
    const std::string& image_type,
    const std::optional<std::string>& expected_error_code = std::nullopt) {
    const std::string context = image_type + " experimental failure output";
    test.expect(document.is_object(), context + " must be a JSON object");
    if (!document.is_object()) {
        return;
    }
    test.expect(
        document.contains("schema_version") &&
            document.at("schema_version") == "m2.multispot.experimental.1",
        context + ".schema_version must equal the isolated experimental version");
    test.expect(
        document.contains("module") && document.at("module") == "m2_image_recognition",
        context + ".module must identify M2");
    test.expect(document.contains("status") && document.at("status") == "error", context + ".status must be error");
    test.expect(
        document.contains("task_id") && document.at("task_id") == task_id,
        context + ".task_id must retain the input task id");
    test.expect(
        document.contains("image_type") && document.at("image_type") == image_type,
        context + ".image_type must identify the failed image");
    test.expect(
        document.contains("experimental") && document.at("experimental").is_boolean() && document.at("experimental").get<bool>(),
        context + ".experimental must be true");
    test.expect(
        document.contains("contract_status") && document.at("contract_status") == "proposed",
        context + ".contract_status must remain proposed");
    test.expect(
        document.contains("validation_status") && document.at("validation_status") == "software_verified",
        context + ".validation_status must remain software_verified");
    test.expect(
        document.contains("validation_scope") && document.at("validation_scope") == "simulation_only",
        context + ".validation_scope must remain simulation_only");
    test.expect(
        document.contains("metrology_validated") && document.at("metrology_validated").is_boolean() &&
            !document.at("metrology_validated").get<bool>(),
        context + ".metrology_validated must be false");
    test.expect(document.contains("error") && document.at("error").is_object(), context + ".error must be an object");
    if (document.contains("error") && document.at("error").is_object()) {
        const auto& error = document.at("error");
        test.expect(
            error.contains("code") && error.at("code").is_string() && !error.at("code").get<std::string>().empty(),
            context + ".error.code must be a non-empty string");
        test.expect(
            error.contains("message") && error.at("message").is_string() && !error.at("message").get<std::string>().empty(),
            context + ".error.message must be a non-empty string");
        if (expected_error_code.has_value()) {
            test.expect(
                error.contains("code") && error.at("code") == *expected_error_code,
                context + ".error.code must match the manifest expectation");
        }
    }
}

std::optional<std::string> readTaskId(
    TestContext& test,
    const std::filesystem::path& input_package,
    const std::string& context) {
    const auto document = readJsonFile(test, input_package, context + " input package");
    if (!document.has_value() || !document->is_object()) {
        return std::nullopt;
    }
    std::string task_id;
    if (!readStringField(test, *document, "task_id", context + " input package", task_id)) {
        return std::nullopt;
    }
    return task_id;
}

void verifyRequiredCoverage(TestContext& test, const std::vector<FixtureCase>& fixtures) {
    const std::set<std::string> required_tags{
        "clean", "grid_25", "grid_94", "shifted", "deformed", "noisy", "gradient", "low_contrast",
        "brightness", "16bit_container", "missing", "extra", "merged", "edge", "abnormal_area", "blank"};
    std::set<std::string> actual_tags;
    std::set<std::string> successful_tags;
    std::set<std::string> failure_tags;
    for (const auto& fixture : fixtures) {
        actual_tags.insert(fixture.tags.begin(), fixture.tags.end());
        if (fixture.expect_success) {
            successful_tags.insert(fixture.tags.begin(), fixture.tags.end());
        } else {
            failure_tags.insert(fixture.tags.begin(), fixture.tags.end());
        }
    }
    for (const auto& required_tag : required_tags) {
        test.expect(
            actual_tags.count(required_tag) == 1,
            "manifest must include a case tagged '" + required_tag + "'");
    }
    const std::set<std::string> required_success_tags{
        "clean", "grid_25", "grid_94", "shifted", "deformed", "noisy", "gradient", "low_contrast", "brightness",
        "16bit_container"};
    for (const auto& required_tag : required_success_tags) {
        test.expect(
            successful_tags.count(required_tag) == 1,
            "manifest tag '" + required_tag + "' must be covered by a successful fixture");
    }
    const std::set<std::string> required_failure_tags{
        "missing", "extra", "merged", "edge", "abnormal_area", "blank"};
    for (const auto& required_tag : required_failure_tags) {
        test.expect(
            failure_tags.count(required_tag) == 1,
            "manifest tag '" + required_tag + "' must be covered by a failure fixture");
    }
}

void expectWarnings(
    TestContext& test,
    const ImageAnalysis& analysis,
    const std::vector<std::string>& expected_warnings,
    const std::string& context) {
    const std::set<std::string> actual{
        analysis.diagnostics.warnings.begin(), analysis.diagnostics.warnings.end()};
    const std::set<std::string> expected{expected_warnings.begin(), expected_warnings.end()};
    test.expect(actual == expected, context + " warning set must exactly match the manifest");
}

void verifyExperimental16BitPolicy(
    TestContext& test,
    const std::filesystem::path& synthetic_root,
    const DetectorLimits& limits) {
    std::ifstream image_stream(
        synthetic_root / "calibration/25_clean_reference.png",
        std::ios::binary);
    const std::vector<unsigned char> image_bytes{
        std::istreambuf_iterator<char>(image_stream),
        std::istreambuf_iterator<char>()};
    const cv::Mat eight_bit = image_bytes.empty()
        ? cv::Mat{}
        : cv::imdecode(image_bytes, cv::IMREAD_GRAYSCALE);
    test.expect(!eight_bit.empty(), "16-bit policy test must load the clean synthetic image");
    if (eight_bit.empty()) {
        return;
    }

    cv::Mat twelve_bit_container;
    eight_bit.convertTo(twelve_bit_container, CV_16U, 4095.0 / 255.0);

    ProcessingConfig config;
    config.recognition_mode = RecognitionMode::HartmannMultispotExperimental;
    config.expected_spot_count = 94;
    config.multispot_min_count = limits.min_spot_count;
    config.multispot_max_count = limits.max_spot_count;

    ImageProcessor processor;
    const ImageAnalysis missing_white_level = processor.processMat(twelve_bit_container, config);
    test.expect(
        !missing_white_level.ok() && missing_white_level.error.code == "CONFIG_INVALID",
        "experimental 16-bit input must fail rather than guess an effective sensor bit depth");

    config.multispot_16bit_white_level = 4095;
    const ImageAnalysis explicit_white_level = processor.processMat(twelve_bit_container, config);
    test.expect(
        explicit_white_level.ok() && explicit_white_level.observations.size() == 25,
        "12-bit data in a 16-bit container must pass with an explicit white level");
    test.expect(
        explicit_white_level.diagnostics.source_depth_bits == 16 &&
            explicit_white_level.diagnostics.normalization_white_level == 4095.0,
        "16-bit diagnostics must retain source depth and explicit normalization white level");

    config.multispot_16bit_white_level = 1023;
    const ImageAnalysis inconsistent_white_level = processor.processMat(twelve_bit_container, config);
    test.expect(
        !inconsistent_white_level.ok() && inconsistent_white_level.error.code == "CONFIG_INVALID",
        "pixels above the configured 16-bit white level must be rejected");
}

void verifyExperimentalSettingsPersistence(
    TestContext& test,
    const std::filesystem::path& system_root) {
    ProcessingConfig config;
    config.recognition_mode = RecognitionMode::HartmannMultispotExperimental;
    config.multispot_min_count = 17;
    config.multispot_max_count = 117;
    config.multispot_min_area_pixels = 19;
    config.multispot_max_area_ratio = 0.03;
    config.multispot_border_margin_pixels = 4;
    config.multispot_background_factor = 1.6;
    config.multispot_min_threshold = 11;
    config.multispot_min_confidence = 0.42;
    config.multispot_16bit_white_level = 4095;
    focimeter::m2::ErrorInfo error;
    const bool loaded = readProcessingConfig(
        system_root / "config/default_config.json", config, error);
    test.expect(loaded, "unified config must load while preserving internal experimental settings");
    test.expect(
        config.recognition_mode == RecognitionMode::HartmannMultispotExperimental &&
            config.multispot_min_count == 17 && config.multispot_max_count == 117 &&
            config.multispot_min_area_pixels == 19 && config.multispot_max_area_ratio == 0.03 &&
            config.multispot_border_margin_pixels == 4 && config.multispot_background_factor == 1.6 &&
            config.multispot_min_threshold == 11 && config.multispot_min_confidence == 0.42 &&
            config.multispot_16bit_white_level == 4095 && config.data_source == "synthetic",
        "readProcessingConfig must not silently reset M2-internal multispot settings");
}

void verifyM1InputPackageIntegration(
    TestContext& test,
    const std::filesystem::path& system_root,
    const std::filesystem::path& output_root) {
    RunOptions options;
    options.input_package = system_root / "data/mock/m1_input_config/input_package_multispot_ok.json";
    options.output_directory = output_root / "m1-input-package";
    options.project_root = system_root;
    options.recognition_mode = RecognitionMode::HartmannMultispotExperimental;

    const ImageRecognitionModule module;
    const auto result = module.run(options);
    test.expect(result.ok(), "M2 must consume an M1 v1 multispot input package");
    if (!result.ok()) {
        return;
    }

    const auto calibration = readJsonFile(test, result.calibration_output, "M1 package calibration output");
    const auto measurement = readJsonFile(test, result.measurement_output, "M1 package measurement output");
    const auto run_log = readJsonFile(test, result.log_output, "M1 package run log");
    for (const auto* document : {&calibration, &measurement}) {
        if (document->has_value()) {
            test.expect(
                document->value().at("data_source") == "synthetic" &&
                    document->value().at("spots").is_array() && document->value().at("spots").size() == 94,
                "M2 experimental output must preserve M1 synthetic provenance and detection count");
        }
    }
    if (run_log.has_value()) {
        test.expect(
            run_log->contains("data_source") && run_log->at("data_source") == "synthetic",
            "M2 run log must derive data_source from the M1 configuration profile");
    }
}

void verifyExperimentalSerializerBehavior(
    TestContext& test,
    const std::filesystem::path& synthetic_root,
    const std::filesystem::path& output_root,
    const DetectorLimits& limits) {
    ProcessingConfig config;
    config.recognition_mode = RecognitionMode::HartmannMultispotExperimental;
    config.multispot_min_count = limits.min_spot_count;
    config.multispot_max_count = limits.max_spot_count;

    ImageProcessor processor;
    ImageAnalysis analysis = processor.processFile(
        synthetic_root / "calibration/25_clean_reference.png", config);
    test.expect(analysis.ok() && !analysis.observations.empty(), "serializer test source image must pass detection");
    if (!analysis.ok() || analysis.observations.empty()) {
        return;
    }

    focimeter::m2::InputPackage input;
    input.task_id = "m2_multispot_serializer_test";
    input.data_source = "synthetic";
    analysis.observations.front().confidence = 0.0;
    analysis.observations.front().quality_flags.push_back("LOW_CONFIDENCE");

    focimeter::m2::ErrorInfo write_error;
    const std::filesystem::path unusable_output = output_root / "serializer-unusable.json";
    test.expect(
        writeExperimentalMultispotSuccess(
            unusable_output, input, "calibration", analysis, config, write_error),
        "serializer must accept a structurally valid result with is_usable=false");
    const auto unusable_document = readJsonFile(test, unusable_output, "serializer unusable output");
    if (unusable_document.has_value()) {
        test.expect(
            unusable_document->contains("quality") && unusable_document->at("quality").is_object() &&
                unusable_document->at("quality").contains("is_usable") &&
                unusable_document->at("quality").at("is_usable").is_boolean() &&
                !unusable_document->at("quality").at("is_usable").get<bool>(),
            "a low-confidence observation must make experimental quality.is_usable false");
        test.expect(
            unusable_document->contains("spots") && unusable_document->at("spots").is_array() &&
                !unusable_document->at("spots").empty() &&
                unusable_document->at("spots").front().contains("quality_flags") &&
                std::find(
                    unusable_document->at("spots").front().at("quality_flags").begin(),
                    unusable_document->at("spots").front().at("quality_flags").end(),
                    "LOW_CONFIDENCE") !=
                    unusable_document->at("spots").front().at("quality_flags").end(),
            "a directly serialized low-confidence observation must retain LOW_CONFIDENCE");
    }

    config.multispot_min_confidence = std::numeric_limits<double>::quiet_NaN();
    write_error = {};
    test.expect(
        !writeExperimentalMultispotSuccess(
            output_root / "serializer-invalid-config.json",
            input,
            "calibration",
            analysis,
            config,
            write_error),
        "serializer must reject a non-finite confidence threshold");

    config.multispot_min_confidence = 0.35;
    analysis.diagnostics.rejected_area_count = -1;
    write_error = {};
    test.expect(
        !writeExperimentalMultispotSuccess(
            output_root / "serializer-invalid-diagnostics.json",
            input,
            "calibration",
            analysis,
            config,
            write_error),
        "serializer must reject invalid diagnostic counters");
}

void runFixture(
    TestContext& test,
    const FixtureCase& fixture,
    const DetectorLimits& limits,
    const std::filesystem::path& system_root,
    const std::filesystem::path& output_root) {
    const auto task_id = readTaskId(test, fixture.input_package, fixture.id);
    if (!task_id.has_value()) {
        return;
    }
    test.expect(
        std::filesystem::is_regular_file(fixture.calibration_image),
        fixture.id + " calibration fixture image must exist");
    test.expect(
        std::filesystem::is_regular_file(fixture.measurement_image),
        fixture.id + " measurement fixture image must exist");
    if (!std::filesystem::is_regular_file(fixture.calibration_image) ||
        !std::filesystem::is_regular_file(fixture.measurement_image)) {
        return;
    }

    ProcessingConfig config;
    config.recognition_mode = RecognitionMode::HartmannMultispotExperimental;
    config.multispot_min_count = limits.min_spot_count;
    config.multispot_max_count = limits.max_spot_count;
    config.multispot_16bit_white_level = fixture.input_white_level;

    ImageProcessor processor;
    const ImageAnalysis calibration_analysis = processor.processFile(fixture.calibration_image, config);
    const ImageAnalysis measurement_analysis = processor.processFile(fixture.measurement_image, config);
    test.expect(
        static_cast<int>(calibration_analysis.observations.size()) == fixture.expected_calibration_count,
        fixture.id + " calibration candidate count must match expected_detection");
    test.expect(
        static_cast<int>(measurement_analysis.observations.size()) == fixture.expected_measurement_count,
        fixture.id + " measurement candidate count must match expected_detection");
    expectWarnings(test, measurement_analysis, fixture.expected_warnings, fixture.id + " measurement diagnostics");
    if (fixture.tags.count("abnormal_area") == 1) {
        test.expect(
            measurement_analysis.diagnostics.raw_candidate_count == 25,
            fixture.id + " must create all 25 connected components before area filtering");
        test.expect(
            measurement_analysis.diagnostics.rejected_area_count == 25,
            fixture.id + " must reject all 25 undersized components by area");
    }
    if (fixture.expect_success) {
        test.expect(calibration_analysis.ok(), fixture.id + " calibration must pass the experimental detector");
        test.expect(measurement_analysis.ok(), fixture.id + " measurement must pass the experimental detector");
        if (calibration_analysis.ok()) {
            expectObservationQuality(test, calibration_analysis, fixture.id + " calibration");
            std::vector<cv::Point2d> points;
            points.reserve(calibration_analysis.observations.size());
            for (const auto& observation : calibration_analysis.observations) {
                points.push_back(observation.center);
            }
            expectPointAccuracy(test, points, fixture.calibration, fixture.id + " calibration direct detector");
        }
        if (measurement_analysis.ok()) {
            expectObservationQuality(test, measurement_analysis, fixture.id + " measurement");
            std::vector<cv::Point2d> points;
            points.reserve(measurement_analysis.observations.size());
            for (const auto& observation : measurement_analysis.observations) {
                points.push_back(observation.center);
            }
            expectPointAccuracy(test, points, fixture.measurement, fixture.id + " measurement direct detector");
        }
    } else {
        test.expect(
            calibration_analysis.ok(),
            fixture.id + " calibration control image must remain valid");
        test.expect(
            !measurement_analysis.ok() && measurement_analysis.error.code == fixture.expected_error_code,
            fixture.id + " measurement must fail with the manifest error code");
    }

    const std::filesystem::path case_output = output_root / fixture.id;
    RunOptions options;
    options.input_package = fixture.input_package;
    options.output_directory = case_output;
    options.project_root = system_root;
    options.recognition_mode = RecognitionMode::HartmannMultispotExperimental;
    options.experimental_16bit_white_level = fixture.input_white_level;

    const ImageRecognitionModule module;
    const auto result = module.run(options);
    const auto run_log = readJsonFile(test, result.log_output, fixture.id + " experimental run log");
    if (run_log.has_value()) {
        test.expect(
            run_log->contains("experimental") && run_log->at("experimental").is_boolean() &&
                run_log->at("experimental").get<bool>(),
            fixture.id + " run log must identify experimental mode");
        test.expect(
            run_log->contains("contract_status") && run_log->at("contract_status") == "proposed" &&
                run_log->contains("data_source") && run_log->at("data_source") == "synthetic" &&
                run_log->contains("validation_status") &&
                run_log->at("validation_status") == "software_verified" &&
                run_log->contains("validation_scope") && run_log->at("validation_scope") == "simulation_only" &&
                run_log->contains("metrology_validated") &&
                run_log->at("metrology_validated").is_boolean() &&
                !run_log->at("metrology_validated").get<bool>(),
            fixture.id + " run log must retain the experimental validation boundary");
    }
    const std::filesystem::path experimental_directory = case_output / "experimental_multispot";
    const std::filesystem::path expected_calibration_output =
        experimental_directory / "spots_calib_multispot.json";
    const std::filesystem::path expected_measurement_output =
        experimental_directory / "spots_meas_multispot.json";
    test.expect(
        result.calibration_output.lexically_normal() == expected_calibration_output.lexically_normal(),
        fixture.id + " calibration RunResult path must use the experimental output location");
    test.expect(
        result.measurement_output.lexically_normal() == expected_measurement_output.lexically_normal(),
        fixture.id + " measurement RunResult path must use the experimental output location");
    test.expect(
        !std::filesystem::exists(case_output / "spots_calib.json") &&
            !std::filesystem::exists(case_output / "spots_meas.json"),
        fixture.id + " experimental mode must not publish v1 five-spot output files");

    const auto calibration_document = readJsonFile(test, expected_calibration_output, fixture.id + " calibration output");
    const auto measurement_document = readJsonFile(test, expected_measurement_output, fixture.id + " measurement output");
    if (!calibration_document.has_value() || !measurement_document.has_value()) {
        return;
    }

    if (!fixture.expect_success) {
        test.expect(
            !result.ok() && result.error.code == fixture.expected_error_code,
            fixture.id + " failure fixture must return the manifest error code");
        expectExperimentalFailureDocument(test, *calibration_document, *task_id, "calibration");
        expectExperimentalFailureDocument(
            test, *measurement_document, *task_id, "measurement", fixture.expected_error_code);
        return;
    }

    test.expect(result.ok(), fixture.id + " success fixture must return a successful RunResult");
    const auto calibration_points = expectExperimentalSuccessDocument(
        test, *calibration_document, *task_id, "calibration", fixture.calibration);
    const auto measurement_points = expectExperimentalSuccessDocument(
        test, *measurement_document, *task_id, "measurement", fixture.measurement);
    if (calibration_points.has_value()) {
        expectPointAccuracy(test, *calibration_points, fixture.calibration, fixture.id + " calibration JSON output");
    }
    if (measurement_points.has_value()) {
        expectPointAccuracy(test, *measurement_points, fixture.measurement, fixture.id + " measurement JSON output");
    }
}

void expectModuleFailure(
    TestContext& test,
    const RunOptions& options,
    const std::string& expected_code,
    const int expected_exit_code,
    const std::string& expected_task_id,
    const std::string& label,
    const bool calibration_uses_primary_error = true) {
    const ImageRecognitionModule module;
    const auto result = module.run(options);
    test.expect(
        !result.ok() && result.exit_code == expected_exit_code && result.error.code == expected_code,
        label + " must return the expected experimental failure; got exit=" +
            std::to_string(result.exit_code) + ", code=" + result.error.code);
    const auto calibration = readJsonFile(test, result.calibration_output, label + " calibration error output");
    const auto measurement = readJsonFile(test, result.measurement_output, label + " measurement error output");
    if (calibration.has_value()) {
        expectExperimentalFailureDocument(
            test,
            *calibration,
            expected_task_id,
            "calibration",
            calibration_uses_primary_error ? std::optional<std::string>(expected_code) : std::nullopt);
    }
    if (measurement.has_value()) {
        expectExperimentalFailureDocument(
            test, *measurement, expected_task_id, "measurement", expected_code);
    }
}

void verifyExperimentalCommonFailures(
    TestContext& test,
    const std::filesystem::path& synthetic_root,
    const std::filesystem::path& system_root,
    const std::filesystem::path& output_root) {
    const auto valid_document = readJsonFile(
        test,
        synthetic_root / "packages/input_package_25_clean_shift.json",
        "common-failure source package");
    if (!valid_document.has_value()) {
        return;
    }

    const std::filesystem::path case_root = output_root / "common-failures";
    RunOptions options;
    options.project_root = system_root;
    options.recognition_mode = RecognitionMode::HartmannMultispotExperimental;

    options.input_package = case_root / "input-not-found.json";
    options.output_directory = case_root / "input-not-found-output";
    expectModuleFailure(test, options, "UNKNOWN_ERROR", 2, "unknown", "missing input JSON");

    options.input_package = case_root / "malformed.json";
    options.output_directory = case_root / "malformed-output";
    test.expect(writeTextFile(options.input_package, "{"), "malformed JSON fixture must be writable");
    expectModuleFailure(test, options, "UNKNOWN_ERROR", 2, "unknown", "malformed input JSON");

    json missing_root_field = *valid_document;
    missing_root_field.erase("task_id");
    options.input_package = case_root / "missing-task-id.json";
    options.output_directory = case_root / "missing-task-id-output";
    test.expect(writeJsonFile(options.input_package, missing_root_field), "missing-task-id fixture must be writable");
    expectModuleFailure(test, options, "UNKNOWN_ERROR", 2, "unknown", "missing root field");

    json missing_data_field = *valid_document;
    missing_data_field["data"].erase("measurement_image");
    options.input_package = case_root / "missing-measurement-field.json";
    options.output_directory = case_root / "missing-measurement-field-output";
    test.expect(
        writeJsonFile(options.input_package, missing_data_field),
        "missing-measurement-field fixture must be writable");
    expectModuleFailure(
        test,
        options,
        "UNKNOWN_ERROR",
        2,
        missing_data_field.at("task_id").get<std::string>(),
        "missing data field");

    json upstream_error = *valid_document;
    upstream_error["status"] = "error";
    options.input_package = case_root / "upstream-error.json";
    options.output_directory = case_root / "upstream-error-output";
    test.expect(writeJsonFile(options.input_package, upstream_error), "upstream-error fixture must be writable");
    expectModuleFailure(
        test, options, "UNKNOWN_ERROR", 2, upstream_error.at("task_id").get<std::string>(), "upstream error");

    json missing_config = *valid_document;
    missing_config["data"]["config_path"] = "config/multispot-missing.json";
    options.input_package = case_root / "missing-config.json";
    options.output_directory = case_root / "missing-config-output";
    test.expect(writeJsonFile(options.input_package, missing_config), "missing-config fixture must be writable");
    expectModuleFailure(
        test, options, "CONFIG_NOT_FOUND", 2, missing_config.at("task_id").get<std::string>(), "missing config");

    json invalid_config = *valid_document;
    invalid_config["data"]["config_path"] =
        "data/mock/m2_image_recognition/synthetic_multispot/README.md";
    options.input_package = case_root / "invalid-config.json";
    options.output_directory = case_root / "invalid-config-output";
    test.expect(writeJsonFile(options.input_package, invalid_config), "invalid-config fixture must be writable");
    expectModuleFailure(
        test, options, "CONFIG_INVALID", 2, invalid_config.at("task_id").get<std::string>(), "invalid config");

    json missing_image = *valid_document;
    missing_image["data"]["measurement_image"] =
        "data/mock/m2_image_recognition/synthetic_multispot/measurement/not-found.png";
    options.input_package = case_root / "missing-image.json";
    options.output_directory = case_root / "missing-image-output";
    test.expect(writeJsonFile(options.input_package, missing_image), "missing-image fixture must be writable");
    expectModuleFailure(
        test,
        options,
        "IMAGE_NOT_FOUND",
        3,
        missing_image.at("task_id").get<std::string>(),
        "missing image",
        false);

    json invalid_image = *valid_document;
    invalid_image["data"]["measurement_image"] =
        "data/mock/m2_image_recognition/synthetic_multispot/README.md";
    options.input_package = case_root / "invalid-image.json";
    options.output_directory = case_root / "invalid-image-output";
    test.expect(writeJsonFile(options.input_package, invalid_image), "invalid-image fixture must be writable");
    expectModuleFailure(
        test,
        options,
        "IMAGE_LOAD_FAILED",
        3,
        invalid_image.at("task_id").get<std::string>(),
        "undecodable image",
        false);

    json invalid_source = *valid_document;
    invalid_source["data_source"] = 42;
    options.input_package = case_root / "invalid-data-source.json";
    options.output_directory = case_root / "invalid-data-source-output";
    test.expect(writeJsonFile(options.input_package, invalid_source), "invalid-data-source fixture must be writable");
    expectModuleFailure(
        test, options, "UNKNOWN_ERROR", 2, invalid_source.at("task_id").get<std::string>(), "invalid experimental data_source");

    const std::filesystem::path local_project = case_root / "dimension-project";
    std::error_code filesystem_error;
    std::filesystem::create_directories(local_project / "config", filesystem_error);
    test.expect(!filesystem_error, "dimension project directory must be creatable");
    std::filesystem::copy_file(
        system_root / "config/default_config.json",
        local_project / "config/default_config.json",
        std::filesystem::copy_options::overwrite_existing,
        filesystem_error);
    test.expect(!filesystem_error, "dimension project config must be copied");
    filesystem_error.clear();
    std::filesystem::copy_file(
        synthetic_root / "calibration/25_clean_reference.png",
        local_project / "calibration.png",
        std::filesystem::copy_options::overwrite_existing,
        filesystem_error);
    test.expect(!filesystem_error, "dimension project calibration must be copied");
    test.expect(
        writePngFile(local_project / "measurement.png", cv::Mat(64, 64, CV_8UC1, cv::Scalar(0))),
        "dimension project measurement must be written");
    json dimension_package = *valid_document;
    dimension_package["task_id"] = "m2_multispot_dimension_mismatch";
    dimension_package["data"]["calibration_image"] = "calibration.png";
    dimension_package["data"]["measurement_image"] = "measurement.png";
    dimension_package["data"]["config_path"] = "config/default_config.json";
    options.project_root = local_project;
    options.input_package = case_root / "dimension-mismatch.json";
    options.output_directory = case_root / "dimension-mismatch-output";
    test.expect(writeJsonFile(options.input_package, dimension_package), "dimension fixture must be writable");
    expectModuleFailure(
        test, options, "COORDINATE_SYSTEM_INVALID", 3, "m2_multispot_dimension_mismatch", "dimension mismatch");

    options.project_root = system_root;
    options.input_package = synthetic_root / "packages/input_package_25_clean_shift.json";
    options.output_directory = case_root / "not-a-directory";
    test.expect(writeTextFile(options.output_directory, "occupied"), "unwritable output fixture must be writable");
    const ImageRecognitionModule module;
    const auto output_failure = module.run(options);
    test.expect(
        !output_failure.ok() && output_failure.exit_code == 4,
        "output path occupied by a file must return an output failure");

    RunOptions five_spot_options;
    five_spot_options.input_package = case_root / "invalid-data-source.json";
    five_spot_options.output_directory = case_root / "five-spot-invalid-data-source-output";
    five_spot_options.project_root = system_root;
    const auto five_spot_result = module.run(five_spot_options);
    test.expect(
        five_spot_result.exit_code == 3,
        "experimental data_source metadata must not change five-spot input acceptance");
}

}  // namespace

int runMultispotTests(const std::vector<std::filesystem::path>& arguments) {
    std::filesystem::path temp_root;
    std::filesystem::path synthetic_root;
    std::filesystem::path system_root;
    for (std::size_t index = 1; index < arguments.size(); ++index) {
        const std::string argument = arguments[index].generic_string();
        if (argument == "--temp-dir" && index + 1 < arguments.size()) {
            temp_root = arguments[++index];
        } else if (argument == "--synthetic-root" && index + 1 < arguments.size()) {
            synthetic_root = arguments[++index];
        } else if (argument == "--system-root" && index + 1 < arguments.size()) {
            system_root = arguments[++index];
        } else {
            std::cerr << "Usage: focimeter_m2_multispot_tests --temp-dir <path> --synthetic-root <path> --system-root <path>\n";
            return 2;
        }
    }
    if (temp_root.empty() || synthetic_root.empty() || system_root.empty()) {
        std::cerr << "--temp-dir, --synthetic-root, and --system-root are required.\n";
        return 2;
    }

    TestContext test;
    const auto loaded_fixtures = loadFixtures(test, synthetic_root);
    if (!loaded_fixtures.has_value()) {
        return 1;
    }
    const DetectorLimits& limits = loaded_fixtures->first;
    const std::vector<FixtureCase>& fixtures = loaded_fixtures->second;
    test.expect(!fixtures.empty(), "manifest must yield at least one valid multispot fixture");
    verifyRequiredCoverage(test, fixtures);
    verifyExperimental16BitPolicy(test, synthetic_root, limits);
    verifyExperimentalSettingsPersistence(test, system_root);

    const auto unique_suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path output_root =
        temp_root / ("focimeter-m2-multispot-owned-run-" + std::to_string(unique_suffix));
    std::error_code filesystem_error;
    std::filesystem::create_directories(output_root, filesystem_error);
    if (filesystem_error) {
        std::cerr << "Could not create multispot test output directory.\n";
        return 2;
    }
    verifyM1InputPackageIntegration(test, system_root, output_root);
    verifyExperimentalSerializerBehavior(test, synthetic_root, output_root / "serializer", limits);

    for (const auto& fixture : fixtures) {
        runFixture(test, fixture, limits, system_root, output_root);
    }
    verifyExperimentalCommonFailures(test, synthetic_root, system_root, output_root);
    std::filesystem::remove_all(output_root, filesystem_error);
    if (filesystem_error) {
        test.expect(false, "test-owned temporary output directory must be removable");
    }

    if (test.failures == 0) {
        std::cout << "All M2 multispot tests passed.\n";
        return 0;
    }
    std::cerr << test.failures << " M2 multispot test assertion(s) failed.\n";
    return 1;
}

#ifdef _WIN32
int wmain(int argc, wchar_t* argv[]) {
    std::vector<std::filesystem::path> arguments;
    arguments.reserve(static_cast<std::size_t>(argc));
    for (int index = 0; index < argc; ++index) {
        arguments.emplace_back(argv[index]);
    }
    return runMultispotTests(arguments);
}
#else
int main(int argc, char* argv[]) {
    std::vector<std::filesystem::path> arguments;
    arguments.reserve(static_cast<std::size_t>(argc));
    for (int index = 0; index < argc; ++index) {
        arguments.emplace_back(std::filesystem::u8path(argv[index]));
    }
    return runMultispotTests(arguments);
}
#endif
