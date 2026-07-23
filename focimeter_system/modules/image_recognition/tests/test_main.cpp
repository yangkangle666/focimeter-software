#include <cmath>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <nlohmann/json.hpp>

#include "focimeter/m2/image_processor.h"
#include "focimeter/m2/module.h"
#include "focimeter/m2/output_lock.h"
#include "focimeter/m2/spot_matcher.h"

namespace {

using focimeter::m2::ErrorInfo;
using focimeter::m2::ImageProcessor;
using focimeter::m2::ImageRecognitionModule;
using focimeter::m2::OutputDirectoryLock;
using focimeter::m2::ProcessingConfig;
using focimeter::m2::RunOptions;
using focimeter::m2::Spot;
using focimeter::m2::SpotMatcher;
using focimeter::m2::SpotObservation;

struct TestContext {
    int failures{0};

    void expect(const bool condition, const std::string& message) {
        if (!condition) {
            ++failures;
            std::cerr << "FAIL: " << message << "\n";
        }
    }
};

std::vector<cv::Point2d> basePoints() {
    return {{512.0, 384.0}, {512.0, 304.0}, {432.0, 384.0}, {512.0, 464.0}, {592.0, 384.0}};
}

cv::Mat render(const std::vector<cv::Point2d>& points) {
    cv::Mat image(768, 1024, CV_8UC3, cv::Scalar(5, 5, 5));
    for (const auto& point : points) {
        cv::circle(image, cv::Point(cvRound(point.x), cvRound(point.y)), 14, cv::Scalar(235, 235, 235), cv::FILLED, cv::LINE_AA);
    }
    return image;
}

bool writePng(const std::filesystem::path& path, const cv::Mat& image) {
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) {
        return false;
    }
    std::vector<unsigned char> bytes;
    if (!cv::imencode(".png", image, bytes)) {
        return false;
    }
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return output.good();
}

bool writeText(const std::filesystem::path& path, const std::string& text) {
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) {
        return false;
    }
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << text;
    return output.good();
}

nlohmann::json readJson(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return nlohmann::json::parse(input);
}

std::string unifiedConfigJson() {
    return R"({
  "schema_version": "1.0",
  "config_name": "test_config",
  "camera": {"pixel_size_um": 4.0, "image_width": null, "image_height": "TODO_CONFIRM"},
  "optical": {"distance_m": 0.03, "hartmann_spacing_mm": null},
  "image_processing": {"roi_width_ratio": 0.9, "roi_height_ratio": 0.9, "median_kernel": 3, "tophat_kernel": 30, "otsu_a": 0.4, "otsu_b": 0.7, "max_depth": 2},
  "recognition": {"expected_spot_count": 5, "min_confidence": 0.7},
  "calculation": {"pixel_threshold": 1.0, "angle_unit": "degree", "diopter_unit": "D"},
  "path_policy": {"path_type": "relative_to_project_root", "allow_absolute_path": false}
})";
}

void expectErrorJsonShape(
    TestContext& test,
    const nlohmann::json& document,
    const std::string& expected_task_id,
    const std::string& expected_image_type) {
    const bool shape_ok = document.is_object() &&
        document.contains("schema_version") && document.at("schema_version").is_string() &&
        document.at("schema_version") == "1.0" &&
        document.contains("task_id") && document.at("task_id").is_string() &&
        document.at("task_id") == expected_task_id &&
        document.contains("module") && document.at("module") == "m2_image_recognition" &&
        document.contains("status") && document.at("status") == "error" &&
        document.contains("image_type") && document.at("image_type") == expected_image_type &&
        document.contains("error") && document.at("error").is_object();
    test.expect(shape_ok, expected_image_type + " error output must have the unified v1 envelope");
    if (!shape_ok) {
        return;
    }
    const auto& error = document.at("error");
    test.expect(
        error.contains("code") && error.at("code").is_string() &&
            error.contains("message") && error.at("message").is_string() &&
            error.contains("module") && error.at("module") == "m2_image_recognition" &&
            error.contains("recoverable") && error.at("recoverable").is_boolean() &&
            error.contains("details") && error.at("details").is_object(),
        expected_image_type + " error object must contain all shared fields and types");
}

void expectSuccessJsonShape(
    TestContext& test,
    const nlohmann::json& document,
    const std::string& expected_task_id,
    const std::string& expected_image_type) {
    test.expect(document.is_object(), expected_image_type + " output must be a JSON object");
    if (!document.is_object()) {
        return;
    }

    const auto hasString = [&document](const char* key) {
        return document.contains(key) && document.at(key).is_string();
    };
    test.expect(
        hasString("schema_version") && document.at("schema_version") == "1.0",
        expected_image_type + " output must contain schema_version 1.0");
    test.expect(
        hasString("task_id") && document.at("task_id") == expected_task_id,
        expected_image_type + " output must retain the input task_id");
    test.expect(
        hasString("module") && document.at("module") == "m2_image_recognition",
        expected_image_type + " output must identify the M2 module");
    test.expect(
        hasString("status") && document.at("status") == "ok",
        expected_image_type + " success output must have status ok");
    test.expect(
        hasString("image_type") && document.at("image_type") == expected_image_type,
        expected_image_type + " output must identify its image type");
    test.expect(
        hasString("coordinate_type") && document.at("coordinate_type") == "image_pixel",
        expected_image_type + " output must declare whole-image pixel coordinates");

    const bool has_spots = document.contains("spots") && document.at("spots").is_array();
    test.expect(has_spots, expected_image_type + " output must contain a spots array");
    if (has_spots) {
        const auto& spots = document.at("spots");
        test.expect(spots.size() == 5, expected_image_type + " output must contain exactly five spots");
        for (const auto& spot : spots) {
            const bool shape_ok = spot.is_object() &&
                spot.contains("spot_id") && spot.at("spot_id").is_number_integer() &&
                spot.contains("role") && spot.at("role").is_string() &&
                spot.contains("x") && spot.at("x").is_number() &&
                spot.contains("y") && spot.at("y").is_number() &&
                spot.contains("confidence") && spot.at("confidence").is_number();
            test.expect(shape_ok, expected_image_type + " spot entries must have the contract field types");
        }
    }

    const bool has_quality = document.contains("quality") && document.at("quality").is_object();
    test.expect(has_quality, expected_image_type + " output must contain a quality object");
    if (has_quality) {
        const auto& quality = document.at("quality");
        test.expect(
            quality.contains("expected_count") && quality.at("expected_count").is_number_integer() &&
                quality.at("expected_count") == 5,
            expected_image_type + " quality.expected_count must be integer 5");
        test.expect(
            quality.contains("detected_count") && quality.at("detected_count").is_number_integer() &&
                quality.at("detected_count") == 5,
            expected_image_type + " quality.detected_count must be integer 5");
        test.expect(
            quality.contains("min_confidence") && quality.at("min_confidence").is_number(),
            expected_image_type + " quality.min_confidence must be numeric");
        test.expect(
            quality.contains("is_usable") && quality.at("is_usable").is_boolean() &&
                quality.at("is_usable").get<bool>(),
            expected_image_type + " quality.is_usable must be true");
        test.expect(
            quality.contains("warnings") && quality.at("warnings").is_array(),
            expected_image_type + " quality.warnings must be an array");
    }
    test.expect(
        document.contains("error") && document.at("error").is_null(),
        expected_image_type + " success output must contain a null error field");
}

std::vector<cv::Point2d> transform(const std::vector<cv::Point2d>& points) {
    const cv::Point2d pivot(512.0, 384.0);
    const double angle = 8.0 * 3.14159265358979323846 / 180.0;
    const double cosine = std::cos(angle);
    const double sine = std::sin(angle);
    std::vector<cv::Point2d> result;
    for (const auto& point : points) {
        const cv::Point2d local = point - pivot;
        result.emplace_back(
            pivot.x + 1.05 * (cosine * local.x - sine * local.y) + 12.0,
            pivot.y + 1.05 * (sine * local.x + cosine * local.y) - 8.0);
    }
    return result;
}

std::vector<SpotObservation> observations(const std::vector<cv::Point2d>& points) {
    std::vector<SpotObservation> result;
    for (const auto& point : points) {
        SpotObservation observation;
        observation.center = point;
        observation.area = 600.0;
        observation.circularity = 0.95;
        observation.mean_intensity = 235.0;
        observation.confidence = 0.95;
        result.push_back(observation);
    }
    return result;
}

void testImagePipeline(TestContext& test) {
    ProcessingConfig config;
    ImageProcessor processor;
    const auto analysis = processor.processMat(render(basePoints()), config);
    test.expect(analysis.ok(), "five synthetic circles should pass the image pipeline");
    test.expect(analysis.observations.size() == 5, "image pipeline should find exactly five circles");
    if (analysis.observations.size() == 5) {
        bool found_center = false;
        for (const auto& observation : analysis.observations) {
            found_center = found_center || cv::norm(observation.center - cv::Point2d(512.0, 384.0)) < 1.0;
            test.expect(observation.confidence >= config.min_confidence, "synthetic circle confidence should meet the threshold");
        }
        test.expect(found_center, "returned centroids must be in whole-image coordinates, not ROI coordinates");
    }

    cv::Mat sixteen_bit;
    render(basePoints()).convertTo(sixteen_bit, CV_16U, 257.0);
    const auto sixteen_bit_analysis = processor.processMat(sixteen_bit, config);
    test.expect(sixteen_bit_analysis.ok(), "unsigned 16-bit input should be normalized and processed");

    cv::Mat unsupported_float;
    render(basePoints()).convertTo(unsupported_float, CV_32F, 1.0 / 255.0);
    const auto unsupported_analysis = processor.processMat(unsupported_float, config);
    test.expect(
        !unsupported_analysis.ok() && unsupported_analysis.error.code == "IMAGE_LOAD_FAILED",
        "unsupported image depth must return IMAGE_LOAD_FAILED instead of throwing");

    ProcessingConfig invalid_config = config;
    invalid_config.median_kernel = 4;
    const auto invalid_config_analysis = processor.processMat(render(basePoints()), invalid_config);
    test.expect(
        !invalid_config_analysis.ok() && invalid_config_analysis.error.code == "CONFIG_INVALID",
        "direct static-library calls must validate processing configuration");

    const auto all_points = basePoints();
    std::vector<cv::Point2d> three_points(all_points.begin(), all_points.begin() + 3);
    const auto missing = processor.processMat(render(three_points), config);
    test.expect(!missing.ok() && missing.error.code == "SPOT_COUNT_MISMATCH", "three spots must return SPOT_COUNT_MISMATCH");

    auto six_points = all_points;
    six_points.emplace_back(680.0, 510.0);
    const auto extra = processor.processMat(render(six_points), config);
    test.expect(!extra.ok() && extra.error.code == "SPOT_COUNT_MISMATCH", "six spots must return SPOT_COUNT_MISMATCH instead of selecting the largest five");

    cv::Mat merged = render(all_points);
    cv::circle(merged, cv::Point(485, 384), 22, cv::Scalar(235, 235, 235), cv::FILLED, cv::LINE_AA);
    const auto merged_result = processor.processMat(merged, config);
    test.expect(!merged_result.ok(), "merged spots must fail rather than fabricate five centroids");
}

void testPairing(TestContext& test) {
    SpotMatcher matcher;
    ErrorInfo error;
    std::vector<Spot> calibration;
    const auto calibration_observations = observations(basePoints());
    test.expect(matcher.assignCalibrationRoles(calibration_observations, calibration, error), "calibration roles should be assigned for a five-point cross");
    test.expect(calibration.size() == 5 && calibration[0].role == "center" && calibration[1].role == "y_positive" && calibration[4].role == "x_positive", "calibration roles must match the shared mock convention");

    const auto expected_measurement = transform(basePoints());
    auto measurement = observations(expected_measurement);
    std::swap(measurement[0], measurement[4]);
    std::swap(measurement[1], measurement[3]);
    std::vector<Spot> matched;
    focimeter::m2::MatchDiagnostics diagnostics;
    ProcessingConfig config;
    test.expect(matcher.matchMeasurement(calibration, measurement, config, matched, diagnostics, error), "measurement order changes must be matched back to calibration identities");
    test.expect(matched.size() == 5, "cross-image matching should return five spots");
    for (const auto& spot : matched) {
        test.expect(cv::norm(spot.center - expected_measurement[static_cast<std::size_t>(spot.spot_id)]) < 0.01, "matched spot ID must retain its physical-ray coordinate");
        test.expect(spot.role == calibration[static_cast<std::size_t>(spot.spot_id)].role, "matched spot role must be stable across images");
    }

    auto conflicting_calibration = calibration;
    conflicting_calibration[4].spot_id = conflicting_calibration[3].spot_id;
    test.expect(
        !matcher.matchMeasurement(
            conflicting_calibration,
            observations(expected_measurement),
            config,
            matched,
            diagnostics,
            error) &&
            error.code == "COORDINATE_SYSTEM_INVALID",
        "duplicate calibration spot IDs must be rejected as a pairing conflict");

    auto invalid_role_calibration = calibration;
    invalid_role_calibration[4].role = "unknown";
    test.expect(
        !matcher.matchMeasurement(
            invalid_role_calibration,
            observations(expected_measurement),
            config,
            matched,
            diagnostics,
            error) &&
            error.code == "COORDINATE_SYSTEM_INVALID",
        "direct static-library callers must not pair invalid calibration roles");

    std::vector<cv::Point2d> anisotropic_measurement;
    const cv::Point2d affine_pivot(512.0, 384.0);
    for (const auto& point : basePoints()) {
        const cv::Point2d local = point - affine_pivot;
        anisotropic_measurement.emplace_back(
            affine_pivot.x + 1.20 * local.x + 9.0,
            affine_pivot.y + 0.80 * local.y - 6.0);
    }
    auto anisotropic_observations = observations(anisotropic_measurement);
    std::swap(anisotropic_observations[1], anisotropic_observations[4]);
    test.expect(
        matcher.matchMeasurement(
            calibration,
            anisotropic_observations,
            config,
            matched,
            diagnostics,
            error),
        "anisotropic lens-like deformation must remain matchable for M3");
    for (const auto& spot : matched) {
        test.expect(
            cv::norm(spot.center - anisotropic_measurement[static_cast<std::size_t>(spot.spot_id)]) < 0.01,
            "affine matching must preserve physical-ray IDs under unequal axis scales");
    }

    auto false_positive = basePoints();
    false_positive[4].x += 23.0;
    test.expect(
        !matcher.matchMeasurement(
            calibration,
            observations(false_positive),
            config,
            matched,
            diagnostics,
            error),
        "a missing ray replaced by a displaced false spot must fail point and center residual checks");

    const std::vector<SpotObservation> ambiguous = observations({
        {512.0, 384.0}, {512.0, 304.0}, {590.0, 380.0}, {595.0, 410.0}, {512.0, 464.0}});
    std::vector<Spot> ignored;
    test.expect(!matcher.assignCalibrationRoles(ambiguous, ignored, error), "ambiguous role geometry must fail instead of filling a free spot ID");

    const double forty_five_degrees = 45.0 * 3.14159265358979323846 / 180.0;
    std::vector<cv::Point2d> rotated_calibration;
    const cv::Point2d pivot(512.0, 384.0);
    for (const auto& point : basePoints()) {
        const cv::Point2d local = point - pivot;
        rotated_calibration.emplace_back(
            pivot.x + std::cos(forty_five_degrees) * local.x - std::sin(forty_five_degrees) * local.y,
            pivot.y + std::sin(forty_five_degrees) * local.x + std::cos(forty_five_degrees) * local.y);
    }
    test.expect(
        !matcher.assignCalibrationRoles(observations(rotated_calibration), ignored, error),
        "calibration axes near the 45-degree role boundary must be rejected");

    ProcessingConfig strict_confidence = config;
    strict_confidence.min_confidence = 0.99;
    test.expect(
        !matcher.matchMeasurement(
            calibration,
            observations(expected_measurement),
            strict_confidence,
            matched,
            diagnostics,
            error),
        "matching must re-check confidence after applying its pairing penalty");
}

void testSyntheticFixtures(
    TestContext& test,
    const std::filesystem::path& synthetic_root) {
    ProcessingConfig config;
    ImageProcessor processor;
    SpotMatcher matcher;
    ErrorInfo error;

    const auto calibration_analysis = processor.processFile(
        synthetic_root / "calibration" / "base_5spots.png", config);
    test.expect(calibration_analysis.ok(), "committed synthetic calibration image should be detectable");
    if (!calibration_analysis.ok()) {
        return;
    }

    std::vector<Spot> calibration;
    test.expect(
        matcher.assignCalibrationRoles(calibration_analysis.observations, calibration, error),
        "committed synthetic calibration roles should be stable");
    if (calibration.size() != 5) {
        return;
    }

    const auto manifest = readJson(synthetic_root / "manifest.json");
    const bool manifest_shape_ok =
        manifest.is_object() &&
        manifest.contains("image_size") && manifest.at("image_size").is_object() &&
        manifest.contains("calibration_spots") && manifest.at("calibration_spots").is_array() &&
        manifest.contains("cases") && manifest.at("cases").is_object();
    test.expect(manifest_shape_ok, "synthetic manifest must contain image_size, calibration_spots, and cases");
    if (!manifest_shape_ok || manifest.at("calibration_spots").size() != 5) {
        return;
    }

    std::vector<cv::Point2d> calibration_truth(5);
    std::vector<bool> calibration_id_seen(5, false);
    for (const auto& expected_spot : manifest.at("calibration_spots")) {
        const bool spot_shape_ok =
            expected_spot.is_object() &&
            expected_spot.contains("spot_id") && expected_spot.at("spot_id").is_number_integer() &&
            expected_spot.contains("x") && expected_spot.at("x").is_number() &&
            expected_spot.contains("y") && expected_spot.at("y").is_number();
        test.expect(spot_shape_ok, "each manifest calibration spot must contain numeric ID, x, and y");
        if (!spot_shape_ok) {
            continue;
        }
        const int spot_id = expected_spot.at("spot_id").get<int>();
        const bool id_ok = spot_id >= 0 && spot_id < 5 && !calibration_id_seen[static_cast<std::size_t>(spot_id)];
        test.expect(id_ok, "manifest calibration spot IDs must be unique and in [0, 4]");
        if (!id_ok) {
            continue;
        }
        calibration_id_seen[static_cast<std::size_t>(spot_id)] = true;
        calibration_truth[static_cast<std::size_t>(spot_id)] = {
            expected_spot.at("x").get<double>(), expected_spot.at("y").get<double>()};
    }
    for (const auto& spot : calibration) {
        const bool id_ok = spot.spot_id >= 0 && spot.spot_id < 5 &&
            calibration_id_seen[static_cast<std::size_t>(spot.spot_id)];
        test.expect(id_ok, "detected calibration IDs must exist in the manifest");
        if (id_ok) {
            test.expect(
                cv::norm(spot.center - calibration_truth[static_cast<std::size_t>(spot.spot_id)]) <= 1.5,
                "calibration spot_id " + std::to_string(spot.spot_id) +
                    " must use whole-image coordinates from the manifest");
        }
    }

    const double image_width = manifest.at("image_size").at("width").get<double>();
    const double image_height = manifest.at("image_size").at("height").get<double>();
    const cv::Point2d pivot(image_width / 2.0, image_height / 2.0);

    const std::vector<std::string> success_cases{
        "measurement/translate_rotate_scale.png",
        "measurement/rotation_25deg.png",
        "measurement/scale_118pct.png",
        "measurement/anisotropic_xy.png",
        "measurement/brightness_only.png",
        "measurement/noise_only.png",
        "measurement/brightness_noise.png",
    };
    for (const auto& relative_path : success_cases) {
        const bool case_exists = manifest.at("cases").contains(relative_path);
        test.expect(case_exists, relative_path + " must be described by the synthetic manifest");
        if (!case_exists) {
            continue;
        }
        const auto& case_spec = manifest.at("cases").at(relative_path);
        test.expect(
            case_spec.contains("expected") && case_spec.at("expected") == "ok",
            relative_path + " must be marked as a successful synthetic case");

        const auto analysis = processor.processFile(synthetic_root / relative_path, config);
        test.expect(analysis.ok(), relative_path + " should pass committed-image detection");
        if (!analysis.ok()) {
            continue;
        }
        std::vector<Spot> matched;
        focimeter::m2::MatchDiagnostics diagnostics;
        test.expect(
            matcher.matchMeasurement(
                calibration, analysis.observations, config, matched, diagnostics, error),
            relative_path + " should preserve a unique cross-image identity mapping");
        test.expect(matched.size() == 5, relative_path + " should produce exactly five paired spots");

        const cv::Point2d translation(
            case_spec.at("translation").at(0).get<double>(),
            case_spec.at("translation").at(1).get<double>());
        for (const auto& spot : matched) {
            const bool id_ok = spot.spot_id >= 0 && spot.spot_id < 5;
            test.expect(id_ok, relative_path + " must return spot IDs in [0, 4]");
            if (!id_ok) {
                continue;
            }
            test.expect(
                spot.role == calibration[static_cast<std::size_t>(spot.spot_id)].role,
                relative_path + " should retain the calibration role for every spot ID");

            const cv::Point2d local =
                calibration_truth[static_cast<std::size_t>(spot.spot_id)] - pivot;
            cv::Point2d expected;
            if (case_spec.contains("affine_matrix")) {
                const auto& matrix = case_spec.at("affine_matrix");
                expected = {
                    pivot.x + matrix.at(0).at(0).get<double>() * local.x +
                        matrix.at(0).at(1).get<double>() * local.y + translation.x,
                    pivot.y + matrix.at(1).at(0).get<double>() * local.x +
                        matrix.at(1).at(1).get<double>() * local.y + translation.y};
            } else {
                const double scale = case_spec.at("scale").get<double>();
                const double radians =
                    case_spec.at("rotation_degrees").get<double>() *
                    3.14159265358979323846 / 180.0;
                expected = {
                    pivot.x + scale * (std::cos(radians) * local.x - std::sin(radians) * local.y) +
                        translation.x,
                    pivot.y + scale * (std::sin(radians) * local.x + std::cos(radians) * local.y) +
                        translation.y};
            }
            test.expect(
                cv::norm(spot.center - expected) <= 1.5,
                relative_path + " spot_id " + std::to_string(spot.spot_id) +
                    " must match the manifest physical-ray coordinate");
        }
    }

    const std::vector<std::string> count_failures{
        "missing_spots_3.png",
        "extra_spot_6.png",
    };
    for (const auto& filename : count_failures) {
        const auto analysis = processor.processFile(
            synthetic_root / "failure" / filename, config);
        test.expect(
            !analysis.ok() && analysis.error.code == "SPOT_COUNT_MISMATCH",
            filename + " should fail with SPOT_COUNT_MISMATCH (actual=" +
                analysis.error.code + ", detected=" +
                std::to_string(analysis.observations.size()) + ")");
    }

    const auto merged = processor.processFile(
        synthetic_root / "failure" / "merged_spots.png", config);
    test.expect(
        !merged.ok() && merged.error.code == "CENTROID_FAILED",
        "merged_spots.png should fail with CENTROID_FAILED (actual=" +
            merged.error.code + ")");

    const auto ambiguous_roles = processor.processFile(
        synthetic_root / "failure" / "ambiguous_roles.png", config);
    test.expect(
        ambiguous_roles.ok(),
        "ambiguous role fixture should still contain five detectable spots (actual=" +
            ambiguous_roles.error.code + ", detected=" +
            std::to_string(ambiguous_roles.observations.size()) + ")");
    std::vector<Spot> ignored;
    if (ambiguous_roles.ok()) {
        test.expect(
            !matcher.assignCalibrationRoles(ambiguous_roles.observations, ignored, error) &&
                error.code == "COORDINATE_SYSTEM_INVALID",
            "ambiguous role fixture must fail role assignment");
    }

    const auto ambiguous_pairing = processor.processFile(
        synthetic_root / "failure" / "ambiguous_pairing_45deg.png", config);
    test.expect(ambiguous_pairing.ok(), "45-degree pairing fixture should contain five detectable spots");
    if (ambiguous_pairing.ok()) {
        std::vector<Spot> matched;
        focimeter::m2::MatchDiagnostics diagnostics;
        test.expect(
            !matcher.matchMeasurement(
                calibration,
                ambiguous_pairing.observations,
                config,
                matched,
                diagnostics,
                error) &&
                error.code == "COORDINATE_SYSTEM_INVALID",
            "45-degree symmetric pairing must be rejected instead of guessing physical IDs");
    }
}

void testModuleIntegration(TestContext& test, const std::filesystem::path& temp) {
    const auto root = temp / "project";
    const auto calibration_path = root / "data" / "samples" / "calibration" / "calibration.png";
    const auto measurement_path = root / "data" / "samples" / "measurement" / "measurement.png";
    test.expect(writePng(calibration_path, render(basePoints())), "test calibration image should be written");
    test.expect(writePng(measurement_path, render(transform(basePoints()))), "test measurement image should be written");
    const std::string config = unifiedConfigJson();
    const std::string input = R"({
  "schema_version": "1.0",
  "task_id": "synthetic_integration",
  "module": "m1_input_config",
  "status": "ok",
  "data": {"calibration_image": "data/samples/calibration/calibration.png", "measurement_image": "data/samples/measurement/measurement.png", "config_path": "config/default_config.json", "run_mode": "local_image"},
  "quality": {"is_usable": true},
  "error": null
})";
    test.expect(writeText(root / "config" / "default_config.json", config), "test config should be written");
    test.expect(writeText(root / "input_package.json", input), "test input package should be written");

    RunOptions options;
    options.input_package = root / "input_package.json";
    options.output_directory = temp / "output";
    options.project_root = root;
    options.save_intermediate = true;
    const ImageRecognitionModule module;
    const auto result = module.run(options);
    test.expect(result.ok(), "module should run end-to-end on synthetic images");
    test.expect(std::filesystem::is_regular_file(result.calibration_output), "calibration JSON should exist");
    test.expect(std::filesystem::is_regular_file(result.measurement_output), "measurement JSON should exist");
    test.expect(std::filesystem::is_regular_file(options.output_directory / "intermediate" / "calibration_binary.png"), "intermediate binary image should exist");

    const auto calibration_json = readJson(result.calibration_output);
    const auto measurement_json = readJson(result.measurement_output);
    const auto run_log_json = readJson(result.log_output);
    expectSuccessJsonShape(test, calibration_json, "synthetic_integration", "calibration");
    expectSuccessJsonShape(test, measurement_json, "synthetic_integration", "measurement");
    test.expect(run_log_json.at("status") == "ok", "a successful paired publication must include a successful run log");
    for (std::size_t index = 0; index < 5; ++index) {
        const auto& calibration_spot = calibration_json.at("spots").at(index);
        const auto& measurement_spot = measurement_json.at("spots").at(index);
        test.expect(calibration_spot.at("spot_id") == static_cast<int>(index), "calibration IDs must be ordered zero through four");
        test.expect(measurement_spot.at("spot_id") == calibration_spot.at("spot_id"), "measurement ID set must match calibration ID set");
        test.expect(measurement_spot.at("role") == calibration_spot.at("role"), "same spot ID must retain the same role");
        test.expect(measurement_spot.at("confidence").get<double>() >= 0.0 && measurement_spot.at("confidence").get<double>() <= 1.0, "confidence must stay in the closed interval zero to one");
    }

    const std::string bad_input = R"({
  "schema_version": "1.0", "task_id": "missing_image", "module": "m1_input_config", "status": "ok",
  "data": {"calibration_image": "data/samples/calibration/calibration.png", "measurement_image": "data/samples/measurement/not_found.png", "config_path": "config/default_config.json", "run_mode": "local_image"},
  "quality": {"is_usable": true}, "error": null
})";
    test.expect(writeText(root / "missing_input.json", bad_input), "missing-image input should be written");
    options.input_package = root / "missing_input.json";
    options.output_directory = temp / "missing-output";
    const auto missing_result = module.run(options);
    test.expect(!missing_result.ok() && missing_result.error.code == "IMAGE_NOT_FOUND", "missing measurement image must return IMAGE_NOT_FOUND");
    const auto missing_error_json = readJson(missing_result.measurement_output);
    test.expect(missing_error_json.at("status") == "error" && missing_error_json.at("error").at("code") == "IMAGE_NOT_FOUND", "missing image failure must be written as the shared error object");
    expectErrorJsonShape(test, readJson(missing_result.calibration_output), "missing_image", "calibration");
    expectErrorJsonShape(test, missing_error_json, "missing_image", "measurement");

    options.input_package = root / "input_package_does_not_exist.json";
    options.output_directory = temp / "missing-input-output";
    const auto missing_input_result = module.run(options);
    test.expect(
        !missing_input_result.ok() && missing_input_result.exit_code == 2,
        "a missing input package must fail with the input/config exit-code class");
    test.expect(
        readJson(missing_input_result.calibration_output).at("status") == "error" &&
            readJson(missing_input_result.measurement_output).at("status") == "error",
        "a missing input package must still produce a complete error pair");

    const auto corrupt_image = root / "data" / "samples" / "measurement" / "corrupt.png";
    test.expect(writeText(corrupt_image, "this is not a PNG"), "corrupt image fixture should be written");
    const std::string corrupt_image_input = R"({
  "schema_version": "1.0", "task_id": "corrupt_image", "module": "m1_input_config", "status": "ok",
  "data": {"calibration_image": "data/samples/calibration/calibration.png", "measurement_image": "data/samples/measurement/corrupt.png", "config_path": "config/default_config.json", "run_mode": "local_image"},
  "quality": {"is_usable": true}, "error": null
})";
    test.expect(writeText(root / "corrupt_image_input.json", corrupt_image_input), "corrupt-image input should be written");
    options.input_package = root / "corrupt_image_input.json";
    options.output_directory = temp / "corrupt-image-output";
    const auto corrupt_image_result = module.run(options);
    test.expect(
        !corrupt_image_result.ok() && corrupt_image_result.error.code == "IMAGE_LOAD_FAILED",
        "an existing but undecodable image must return IMAGE_LOAD_FAILED");
    test.expect(
        readJson(corrupt_image_result.measurement_output).at("error").at("code") == "IMAGE_LOAD_FAILED",
        "the undecodable-image error must be serialized for the affected measurement output");

    const std::string upstream_error_input = R"({
  "schema_version": "1.0", "task_id": "upstream_error", "module": "m1_input_config", "status": "error",
  "data": {"calibration_image": "data/samples/calibration/calibration.png", "measurement_image": "data/samples/measurement/measurement.png", "config_path": "config/default_config.json", "run_mode": "local_image"},
  "quality": {"is_usable": false}, "error": {"code": "IMAGE_NOT_FOUND"}
})";
    test.expect(writeText(root / "upstream_error.json", upstream_error_input), "upstream-error input should be written");
    options.input_package = root / "upstream_error.json";
    options.output_directory = temp / "upstream-error-output";
    const auto upstream_error_result = module.run(options);
    test.expect(!upstream_error_result.ok(), "M2 must reject an upstream status=error package");

    const std::string absolute_path_input = R"({
  "schema_version": "1.0", "task_id": "absolute_path", "module": "m1_input_config", "status": "ok",
  "data": {"calibration_image": "C:/private/calibration.png", "measurement_image": "data/samples/measurement/measurement.png", "config_path": "config/default_config.json", "run_mode": "local_image"},
  "quality": {"is_usable": true}, "error": null
})";
    test.expect(writeText(root / "absolute_path.json", absolute_path_input), "absolute-path input should be written");
    options.input_package = root / "absolute_path.json";
    options.output_directory = temp / "absolute-path-output";
    const auto absolute_path_result = module.run(options);
    test.expect(!absolute_path_result.ok(), "M2 must reject personal absolute paths");

    const std::string traversal_path_input = R"({
  "schema_version": "1.0", "task_id": "parent_traversal", "module": "m1_input_config", "status": "ok",
  "data": {"calibration_image": "../outside.png", "measurement_image": "data/samples/measurement/measurement.png", "config_path": "config/default_config.json", "run_mode": "local_image"},
  "quality": {"is_usable": true}, "error": null
})";
    test.expect(writeText(root / "parent_traversal.json", traversal_path_input), "parent-traversal input should be written");
    options.input_package = root / "parent_traversal.json";
    options.output_directory = temp / "parent-traversal-output";
    const auto traversal_path_result = module.run(options);
    test.expect(
        !traversal_path_result.ok() && traversal_path_result.exit_code == 2,
        "M2 must reject parent traversal before resolving or probing the path");
    test.expect(
        readJson(traversal_path_result.calibration_output).at("status") == "error" &&
            readJson(traversal_path_result.measurement_output).at("status") == "error",
        "parent traversal must produce the standard paired error outputs");

    const std::string config_traversal_input = R"({
  "schema_version": "1.0", "task_id": "config_parent_traversal", "module": "m1_input_config", "status": "ok",
  "data": {"calibration_image": "data/samples/calibration/calibration.png", "measurement_image": "data/samples/measurement/measurement.png", "config_path": "../outside.json", "run_mode": "local_image"},
  "quality": {"is_usable": true}, "error": null
})";
    test.expect(
        writeText(root / "config_parent_traversal.json", config_traversal_input),
        "config-parent-traversal input should be written");
    options.input_package = root / "config_parent_traversal.json";
    options.output_directory = temp / "config-parent-traversal-output";
    options.project_root.clear();
    const auto config_traversal_result = module.run(options);
    test.expect(
        !config_traversal_result.ok() && config_traversal_result.exit_code == 2,
        "unsafe config paths must fail before automatic project-root discovery probes them");
    test.expect(
        readJson(config_traversal_result.calibration_output).at("status") == "error" &&
            readJson(config_traversal_result.measurement_output).at("status") == "error",
        "unsafe config paths must produce the standard paired error outputs");

    const auto traversal_alias_directory = temp / "raw-traversal-alias-output";
    const auto traversal_alias_path = traversal_alias_directory / "spots_calib.json";
    test.expect(
        writeText(traversal_alias_path, unifiedConfigJson()),
        "raw traversal alias sentinel should be written");
    const std::string config_traversal_alias_input = R"({
  "schema_version": "1.0", "task_id": "config_parent_traversal_alias", "module": "m1_input_config", "status": "ok",
  "data": {"calibration_image": "data/samples/calibration/calibration.png", "measurement_image": "data/samples/measurement/measurement.png", "config_path": "../raw-traversal-alias-output/spots_calib.json", "run_mode": "local_image"},
  "quality": {"is_usable": true}, "error": null
})";
    test.expect(
        writeText(root / "config_parent_traversal_alias.json", config_traversal_alias_input),
        "config-parent-traversal-alias input should be written");
    options.input_package = root / "config_parent_traversal_alias.json";
    options.output_directory = traversal_alias_directory;
    const auto config_traversal_alias_result = module.run(options);
    test.expect(
        !config_traversal_alias_result.ok() && config_traversal_alias_result.exit_code == 4,
        "raw input/output aliases must fail before invalid-path error output cleanup");
    test.expect(
        std::filesystem::is_regular_file(traversal_alias_path) &&
            readJson(traversal_alias_path).at("config_name") == "test_config",
        "raw input/output alias rejection must preserve the declared input sentinel");
    options.project_root = root;

    const std::string unsupported_version_input = R"({
  "schema_version": "2.0", "task_id": "unsupported_version", "module": "m1_input_config", "status": "ok",
  "data": {"calibration_image": "data/samples/calibration/calibration.png", "measurement_image": "data/samples/measurement/measurement.png", "config_path": "config/default_config.json", "run_mode": "local_image"},
  "quality": {"is_usable": true}, "error": null
})";
    test.expect(writeText(root / "unsupported_version.json", unsupported_version_input), "unsupported-version input should be written");
    options.input_package = root / "unsupported_version.json";
    options.output_directory = temp / "unsupported-version-output";
    const auto unsupported_version_result = module.run(options);
    test.expect(
        !unsupported_version_result.ok() && unsupported_version_result.exit_code == 2,
        "unsupported input schema versions must fail as input errors");
    expectErrorJsonShape(
        test,
        readJson(unsupported_version_result.calibration_output),
        "unsupported_version",
        "calibration");
    test.expect(
        readJson(unsupported_version_result.log_output).at("schema_version") == "1.0",
        "run logs must declare the M2-supported schema instead of an unsupported input version");

    const auto alias_directory = temp / "alias-output";
    const auto alias_input = alias_directory / "spots_calib.json";
    test.expect(writeText(alias_input, input), "input/output alias fixture should be written");
    options.input_package = alias_input;
    options.output_directory = alias_directory;
    const auto alias_result = module.run(options);
    test.expect(
        !alias_result.ok() && alias_result.exit_code == 4,
        "an input path overlapping a managed output must fail before cleanup");
    test.expect(
        std::filesystem::is_regular_file(alias_input) && readJson(alias_input).at("module") == "m1_input_config",
        "input/output alias rejection must not delete or overwrite the input package");

    const auto config_alias_directory = root / "config" / "alias-output";
    const auto config_alias_path = config_alias_directory / "spots_calib.json";
    const std::string config_alias_input = R"({
  "schema_version": "1.0", "task_id": "config_alias", "module": "m1_input_config", "status": "ok",
  "data": {"calibration_image": "data/samples/calibration/calibration.png", "measurement_image": "data/samples/measurement/measurement.png", "config_path": "config/alias-output/spots_calib.json", "run_mode": "local_image"},
  "quality": {"is_usable": true}, "error": null
})";
    test.expect(writeText(config_alias_path, unifiedConfigJson()), "config/output alias fixture should be written");
    test.expect(writeText(root / "config_alias_input.json", config_alias_input), "config-alias input should be written");
    options.input_package = root / "config_alias_input.json";
    options.output_directory = config_alias_directory;
    const auto config_alias_result = module.run(options);
    test.expect(
        !config_alias_result.ok() && config_alias_result.exit_code == 4,
        "a config path overlapping a managed output must fail before cleanup");
    test.expect(
        std::filesystem::is_regular_file(config_alias_path) &&
            readJson(config_alias_path).at("config_name") == "test_config",
        "config/output alias rejection must preserve the unified config file");

    const std::string malformed = "{ this is not JSON";
    test.expect(writeText(root / "malformed.json", malformed), "malformed input should be written");
    options.input_package = root / "malformed.json";
    options.output_directory = temp / "output";
    const auto malformed_result = module.run(options);
    test.expect(!malformed_result.ok(), "malformed input JSON must fail cleanly");
    const auto malformed_calibration = readJson(malformed_result.calibration_output);
    const auto malformed_measurement = readJson(malformed_result.measurement_output);
    const auto malformed_log = readJson(malformed_result.log_output);
    test.expect(
        malformed_calibration.at("status") == "error" && malformed_measurement.at("status") == "error",
        "input parse failure must invalidate old success files with a complete error pair");
    test.expect(
        malformed_log.at("status") == "error",
        "input parse failure must replace the previous successful run log");
    test.expect(
        !std::filesystem::exists(options.output_directory / "intermediate" / "calibration_binary.png"),
        "input parse failure must remove known intermediate images from the previous run");

    const std::string commented_json = R"({
  // Comments are not part of the project JSON contract.
  "schema_version": "1.0", "task_id": "commented_json", "module": "m1_input_config", "status": "ok",
  "data": {"calibration_image": "data/samples/calibration/calibration.png", "measurement_image": "data/samples/measurement/measurement.png", "config_path": "config/default_config.json", "run_mode": "local_image"},
  "quality": {"is_usable": true}, "error": null
})";
    test.expect(writeText(root / "commented.json", commented_json), "commented JSON input should be written");
    options.input_package = root / "commented.json";
    options.output_directory = temp / "commented-json-output";
    const auto commented_json_result = module.run(options);
    test.expect(
        !commented_json_result.ok() && commented_json_result.exit_code == 2,
        "non-standard JSON comments must be rejected instead of silently ignored");

    const std::string missing_field = R"({
  "schema_version": "1.0", "task_id": "missing_field", "module": "m1_input_config", "status": "ok",
  "data": {"calibration_image": "data/samples/calibration/calibration.png", "config_path": "config/default_config.json", "run_mode": "local_image"},
  "quality": {"is_usable": true}, "error": null
})";
    test.expect(writeText(root / "missing_field.json", missing_field), "missing-field input should be written");
    options.input_package = root / "missing_field.json";
    options.output_directory = temp / "missing-field-output";
    const auto missing_field_result = module.run(options);
    test.expect(!missing_field_result.ok(), "missing required input fields must fail cleanly");

    const std::string invalid_config_input = R"({
  "schema_version": "1.0", "task_id": "invalid_config", "module": "m1_input_config", "status": "ok",
  "data": {"calibration_image": "data/samples/calibration/calibration.png", "measurement_image": "data/samples/measurement/measurement.png", "config_path": "config/invalid.json", "run_mode": "local_image"},
  "quality": {"is_usable": true}, "error": null
})";
    test.expect(writeText(root / "config" / "invalid.json", R"({"schema_version":"1.0","image_processing":{"roi_width_ratio":1.5},"recognition":{"expected_spot_count":5}})"), "invalid config should be written");
    test.expect(writeText(root / "invalid_config_input.json", invalid_config_input), "invalid-config input should be written");
    options.input_package = root / "invalid_config_input.json";
    options.output_directory = temp / "invalid-config-output";
    const auto invalid_config_result = module.run(options);
    test.expect(!invalid_config_result.ok() && invalid_config_result.error.code == "CONFIG_INVALID", "invalid processing config must return CONFIG_INVALID");

    auto extra_field_config = nlohmann::json::parse(unifiedConfigJson());
    extra_field_config["private_override"] = true;
    test.expect(
        writeText(root / "config" / "extra_field.json", extra_field_config.dump(2)),
        "extra-field config should be written");
    const std::string extra_field_config_input = R"({
  "schema_version": "1.0", "task_id": "extra_config_field", "module": "m1_input_config", "status": "ok",
  "data": {"calibration_image": "data/samples/calibration/calibration.png", "measurement_image": "data/samples/measurement/measurement.png", "config_path": "config/extra_field.json", "run_mode": "local_image"},
  "quality": {"is_usable": true}, "error": null
})";
    test.expect(writeText(root / "extra_field_config_input.json", extra_field_config_input), "extra-config-field input should be written");
    options.input_package = root / "extra_field_config_input.json";
    options.output_directory = temp / "extra-config-field-output";
    const auto extra_field_config_result = module.run(options);
    test.expect(
        !extra_field_config_result.ok() && extra_field_config_result.error.code == "CONFIG_INVALID",
        "unified config must reject undeclared top-level fields like the M3 schema");

    auto bad_path_policy_config = nlohmann::json::parse(unifiedConfigJson());
    bad_path_policy_config["path_policy"]["allow_absolute_path"] = true;
    test.expect(
        writeText(root / "config" / "bad_path_policy.json", bad_path_policy_config.dump(2)),
        "bad-path-policy config should be written");
    const std::string bad_path_policy_input = R"({
  "schema_version": "1.0", "task_id": "bad_path_policy", "module": "m1_input_config", "status": "ok",
  "data": {"calibration_image": "data/samples/calibration/calibration.png", "measurement_image": "data/samples/measurement/measurement.png", "config_path": "config/bad_path_policy.json", "run_mode": "local_image"},
  "quality": {"is_usable": true}, "error": null
})";
    test.expect(writeText(root / "bad_path_policy_input.json", bad_path_policy_input), "bad-path-policy input should be written");
    options.input_package = root / "bad_path_policy_input.json";
    options.output_directory = temp / "bad-path-policy-output";
    const auto bad_path_policy_result = module.run(options);
    test.expect(
        !bad_path_policy_result.ok() && bad_path_policy_result.error.code == "CONFIG_INVALID",
        "unified config must enforce relative paths and forbid absolute paths");

    std::string commented_config = unifiedConfigJson();
    commented_config.insert(commented_config.find('{') + 1, "\n  // Non-standard JSON comment.");
    test.expect(writeText(root / "config" / "commented.json", commented_config), "commented config should be written");
    const std::string commented_config_input = R"({
  "schema_version": "1.0", "task_id": "commented_config", "module": "m1_input_config", "status": "ok",
  "data": {"calibration_image": "data/samples/calibration/calibration.png", "measurement_image": "data/samples/measurement/measurement.png", "config_path": "config/commented.json", "run_mode": "local_image"},
  "quality": {"is_usable": true}, "error": null
})";
    test.expect(writeText(root / "commented_config_input.json", commented_config_input), "commented-config input should be written");
    options.input_package = root / "commented_config_input.json";
    options.output_directory = temp / "commented-config-output";
    const auto commented_config_result = module.run(options);
    test.expect(
        !commented_config_result.ok() && commented_config_result.error.code == "CONFIG_INVALID",
        "configuration JSON comments must be rejected instead of silently ignored");

    const std::string missing_config_input = R"({
  "schema_version": "1.0", "task_id": "missing_config", "module": "m1_input_config", "status": "ok",
  "data": {"calibration_image": "data/samples/calibration/calibration.png", "measurement_image": "data/samples/measurement/measurement.png", "config_path": "config/not_found.json", "run_mode": "local_image"},
  "quality": {"is_usable": true}, "error": null
})";
    test.expect(writeText(root / "missing_config_input.json", missing_config_input), "missing-config input should be written");
    options.input_package = root / "missing_config_input.json";
    options.output_directory = temp / "missing-config-output";
    const auto missing_config_result = module.run(options);
    test.expect(!missing_config_result.ok() && missing_config_result.error.code == "CONFIG_NOT_FOUND", "missing config must return CONFIG_NOT_FOUND");

    options.input_package = root / "input_package.json";
    options.output_directory = temp / "blocked-output";
    test.expect(writeText(options.output_directory, "not a directory"), "blocking output file should be written");
    const auto blocked_output_result = module.run(options);
    test.expect(
        !blocked_output_result.ok() && blocked_output_result.exit_code == 4,
        "unusable output directory must fail with the output-write exit-code class");

    const auto lock_directory = temp / "locked-output";
    OutputDirectoryLock first_lock;
    OutputDirectoryLock second_lock;
    ErrorInfo first_lock_error;
    ErrorInfo second_lock_error;
    test.expect(first_lock.acquire(lock_directory, first_lock_error), "first output-directory writer should acquire the lock");
    test.expect(
        !first_lock.acquire(temp / "another-locked-output", first_lock_error),
        "one lock object must reject a second acquisition instead of leaking its first lock");
    test.expect(
        !second_lock.acquire(lock_directory, second_lock_error),
        "a second writer must not acquire the same output-directory lock");
}

}  // namespace

int runTests(const std::vector<std::filesystem::path>& arguments) {
    std::filesystem::path temp_root;
    std::filesystem::path synthetic_root;
    std::string only;
    for (std::size_t index = 1; index < arguments.size(); ++index) {
        const std::string argument = arguments[index].generic_string();
        if (argument == "--temp-dir" && index + 1 < arguments.size()) {
            temp_root = arguments[++index];
        } else if (argument == "--synthetic-root" && index + 1 < arguments.size()) {
            synthetic_root = arguments[++index];
        } else if (argument == "--only" && index + 1 < arguments.size()) {
            only = arguments[++index].generic_string();
        } else {
            std::cerr << "Unknown or incomplete test option.\n";
            return 2;
        }
    }
    if (temp_root.empty()) {
        std::cerr << "--temp-dir is required by the CTest configuration.\n";
        return 2;
    }
    if (synthetic_root.empty()) {
        std::cerr << "--synthetic-root is required by the CTest configuration.\n";
        return 2;
    }
    if (!only.empty() && only != "image" && only != "pairing" && only != "integration") {
        std::cerr << "--only must be image, pairing, or integration.\n";
        return 2;
    }

    const auto unique_suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto temp_directory =
        temp_root / ("focimeter-m2-owned-run-" + std::to_string(unique_suffix));
    std::error_code cleanup_error;
    std::filesystem::create_directories(temp_directory, cleanup_error);
    if (cleanup_error) {
        std::cerr << "Could not create test directory.\n";
        return 2;
    }

    TestContext test;
    if (only.empty() || only == "image") {
        testImagePipeline(test);
    }
    if (only.empty() || only == "pairing") {
        testPairing(test);
        testSyntheticFixtures(test, synthetic_root);
    }
    if (only.empty() || only == "integration") {
        testModuleIntegration(test, temp_directory);
    }
    std::filesystem::remove_all(temp_directory, cleanup_error);
    if (test.failures == 0) {
        std::cout << "All M2 tests passed.\n";
        return 0;
    }
    std::cerr << test.failures << " M2 test assertion(s) failed.\n";
    return 1;
}

#ifdef _WIN32
int wmain(int argc, wchar_t* argv[]) {
    std::vector<std::filesystem::path> arguments;
    arguments.reserve(static_cast<std::size_t>(argc));
    for (int index = 0; index < argc; ++index) {
        arguments.emplace_back(argv[index]);
    }
    return runTests(arguments);
}
#else
int main(int argc, char* argv[]) {
    std::vector<std::filesystem::path> arguments;
    arguments.reserve(static_cast<std::size_t>(argc));
    for (int index = 0; index < argc; ++index) {
        arguments.emplace_back(std::filesystem::u8path(argv[index]));
    }
    return runTests(arguments);
}
#endif
