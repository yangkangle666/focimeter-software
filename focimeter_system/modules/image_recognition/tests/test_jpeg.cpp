#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include "focimeter/m2/image_processor.h"
#include "focimeter/m2/json_io.h"

namespace {

using focimeter::m2::ImageAnalysis;
using focimeter::m2::ImageProcessor;
using focimeter::m2::InputPackage;
using focimeter::m2::ProcessingConfig;
using focimeter::m2::RecognitionMode;
using focimeter::m2::SpotObservation;
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

bool writeJpeg(const std::filesystem::path& path, const cv::Mat& image, const int quality) {
    std::vector<unsigned char> bytes;
    if (!cv::imencode(".jpg", image, bytes, {cv::IMWRITE_JPEG_QUALITY, quality})) {
        return false;
    }
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) {
        return false;
    }
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(
        reinterpret_cast<const char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size()));
    return output.good();
}

bool writeTiff(const std::filesystem::path& path, const cv::Mat& image) {
    std::vector<unsigned char> bytes;
    if (!cv::imencode(".tif", image, bytes)) {
        return false;
    }
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) {
        return false;
    }
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(
        reinterpret_cast<const char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size()));
    return output.good();
}

std::optional<std::string> readText(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return std::nullopt;
    }
    return std::string(
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>());
}

cv::Mat makeGreenBgr(const cv::Mat& gray) {
    cv::Mat zeros = cv::Mat::zeros(gray.size(), CV_8UC1);
    cv::Mat result;
    cv::merge(std::vector<cv::Mat>{zeros, gray, zeros}, result);
    return result;
}

cv::Mat makeRedBgr(const cv::Mat& gray) {
    cv::Mat zeros = cv::Mat::zeros(gray.size(), CV_8UC1);
    cv::Mat result;
    cv::merge(std::vector<cv::Mat>{zeros, zeros, gray}, result);
    return result;
}

struct MatchMetrics {
    bool complete{false};
    double rms{std::numeric_limits<double>::infinity()};
    double maximum{std::numeric_limits<double>::infinity()};
};

MatchMetrics compareCenters(
    const std::vector<SpotObservation>& actual,
    const std::vector<SpotObservation>& expected) {
    if (actual.size() != expected.size()) {
        return {};
    }
    struct Candidate {
        double distance;
        std::size_t actual_index;
        std::size_t expected_index;
    };
    std::vector<Candidate> candidates;
    candidates.reserve(actual.size() * expected.size());
    for (std::size_t actual_index = 0; actual_index < actual.size(); ++actual_index) {
        for (std::size_t expected_index = 0; expected_index < expected.size(); ++expected_index) {
            candidates.push_back({
                cv::norm(actual[actual_index].center - expected[expected_index].center),
                actual_index,
                expected_index});
        }
    }
    std::sort(candidates.begin(), candidates.end(), [](const Candidate& left, const Candidate& right) {
        return left.distance < right.distance;
    });
    std::vector<bool> actual_used(actual.size(), false);
    std::vector<bool> expected_used(expected.size(), false);
    double sum_squared = 0.0;
    double maximum = 0.0;
    std::size_t matched = 0;
    for (const auto& candidate : candidates) {
        if (actual_used[candidate.actual_index] || expected_used[candidate.expected_index]) {
            continue;
        }
        actual_used[candidate.actual_index] = true;
        expected_used[candidate.expected_index] = true;
        sum_squared += candidate.distance * candidate.distance;
        maximum = std::max(maximum, candidate.distance);
        ++matched;
    }
    if (matched != expected.size()) {
        return {};
    }
    return {
        true,
        std::sqrt(sum_squared / static_cast<double>(matched)),
        maximum};
}

bool sameObservation(
    const SpotObservation& left,
    const SpotObservation& right) {
    return left.center == right.center &&
        left.area == right.area &&
        left.circularity == right.circularity &&
        left.bounding_box_elongation == right.bounding_box_elongation &&
        left.principal_axis_elongation == right.principal_axis_elongation &&
        left.mean_intensity == right.mean_intensity &&
        left.peak_intensity == right.peak_intensity &&
        left.peak_residual_intensity == right.peak_residual_intensity &&
        left.integrated_intensity == right.integrated_intensity &&
        left.confidence == right.confidence &&
        left.quality_flags == right.quality_flags;
}

cv::Mat makeRealisticGreenFixture() {
    cv::Mat image(1200, 1600, CV_8UC3, cv::Scalar(1, 2, 1));
    int index = 0;
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 5; ++column) {
            const cv::Point center(180 + column * 300, 150 + row * 280);
            cv::circle(image, center, 24 + (index % 4) * 3, cv::Scalar(8, 230, 7), cv::FILLED, cv::LINE_AA);
            if (index % 3 == 0) {
                cv::circle(image, center + cv::Point(2, -1), 6, cv::Scalar(2, 18, 2), cv::FILLED, cv::LINE_AA);
            } else {
                cv::circle(image, center, 5, cv::Scalar(245, 255, 245), cv::FILLED, cv::LINE_AA);
            }
            ++index;
        }
    }
    cv::circle(image, cv::Point(3, 610), 8, cv::Scalar(5, 240, 5), cv::FILLED, cv::LINE_AA);
    cv::circle(image, cv::Point(3, 610), 2, cv::Scalar(245, 255, 245), cv::FILLED, cv::LINE_AA);
    for (const cv::Point point : std::vector<cv::Point>{{73, 89}, {525, 537}, {989, 774}, {1401, 1110}}) {
        cv::circle(image, point, 2, cv::Scalar(240, 240, 240), cv::FILLED, cv::LINE_AA);
        cv::circle(image, point + cv::Point(11, 3), 2, cv::Scalar(5, 5, 245), cv::FILLED, cv::LINE_AA);
    }
    // One optical spot with a separated bright core exercises fragment suppression
    // without pretending that two equally sized touching spots are one detection.
    cv::circle(image, cv::Point(1490, 600), 30, cv::Scalar(8, 230, 7), cv::FILLED, cv::LINE_AA);
    cv::circle(image, cv::Point(1490, 600), 15, cv::Scalar(2, 18, 2), cv::FILLED, cv::LINE_AA);
    cv::circle(image, cv::Point(1490, 600), 8, cv::Scalar(245, 255, 245), cv::FILLED, cv::LINE_AA);
    return image;
}

cv::Mat makeCandidateFilterFixture(
    const bool include_near_edge,
    const bool include_clipped_edge,
    const bool include_close_pair,
    const bool include_diagonal_merge) {
    cv::Mat image(420, 420, CV_8UC3, cv::Scalar(1, 2, 1));
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 4; ++column) {
            cv::circle(
                image,
                cv::Point(80 + column * 80, 70 + row * 90),
                8,
                cv::Scalar(8, 230, 7),
                cv::FILLED,
                cv::LINE_AA);
        }
    }
    if (include_near_edge) {
        cv::circle(image, cv::Point(12, 190), 8, cv::Scalar(8, 230, 7), cv::FILLED, cv::LINE_AA);
    }
    if (include_clipped_edge) {
        cv::circle(image, cv::Point(0, 205), 3, cv::Scalar(8, 230, 7), cv::FILLED, cv::LINE_AA);
    }
    if (include_close_pair) {
        cv::circle(image, cv::Point(275, 365), 8, cv::Scalar(8, 230, 7), cv::FILLED, cv::LINE_AA);
        cv::circle(image, cv::Point(297, 365), 8, cv::Scalar(8, 230, 7), cv::FILLED, cv::LINE_AA);
    }
    if (include_diagonal_merge) {
        cv::circle(image, cv::Point(300, 350), 8, cv::Scalar(8, 230, 7), cv::FILLED, cv::LINE_AA);
        cv::circle(image, cv::Point(312, 362), 8, cv::Scalar(8, 230, 7), cv::FILLED, cv::LINE_AA);
    }
    return image;
}

cv::Mat makeUnequalNearPairFixture() {
    cv::Mat image = makeCandidateFilterFixture(false, false, false, false);
    cv::circle(image, cv::Point(270, 365), 13, cv::Scalar(8, 230, 7), cv::FILLED, cv::LINE_AA);
    cv::circle(image, cv::Point(294, 365), 7, cv::Scalar(8, 230, 7), cv::FILLED, cv::LINE_AA);
    return image;
}

cv::Mat makeUnequalNearPairWithContinuousHaloFixture() {
    cv::Mat image = makeCandidateFilterFixture(false, false, false, false);
    cv::rectangle(
        image,
        cv::Rect(245, 343, 75, 45),
        cv::Scalar(2, 35, 2),
        cv::FILLED);
    cv::circle(image, cv::Point(270, 365), 13, cv::Scalar(8, 230, 7), cv::FILLED, cv::LINE_AA);
    cv::circle(image, cv::Point(294, 365), 7, cv::Scalar(8, 230, 7), cv::FILLED, cv::LINE_AA);
    return image;
}

struct GridFixture {
    cv::Mat image;
    std::vector<cv::Point2d> expected_centers;
};

GridFixture makeSizeAndBrightnessVariationFixture() {
    GridFixture fixture;
    fixture.image = cv::Mat(960, 1280, CV_8UC3, cv::Scalar(1, 2, 1));
    constexpr int kRows = 4;
    constexpr int kColumns = 5;
    constexpr int kPitchX = 230;
    constexpr int kPitchY = 210;
    const cv::Point origin(150, 150);
    for (int row = 0; row < kRows; ++row) {
        for (int column = 0; column < kColumns; ++column) {
            const cv::Point center(origin.x + column * kPitchX, origin.y + row * kPitchY);
            const int variant = (row * kColumns + column) % 4;
            const int radius = variant == 0 ? 5 : 7 + variant * 2;
            const int green = 150 + variant * 30;
            cv::circle(
                fixture.image, center, radius, cv::Scalar(5, green, 4), cv::FILLED, cv::LINE_AA);
            if (variant == 2) {
                cv::circle(
                    fixture.image,
                    center,
                    radius / 3,
                    cv::Scalar(2, 20, 2),
                    cv::FILLED,
                    cv::LINE_AA);
            } else if (variant == 3) {
                cv::circle(
                    fixture.image,
                    center,
                    radius / 4,
                    cv::Scalar(245, 255, 245),
                    cv::FILLED,
                    cv::LINE_AA);
            }
            fixture.expected_centers.emplace_back(center);
        }
    }

    // Isolated bright dust must not pass the recovery rule without lattice neighbors.
    for (const cv::Point dust :
         std::vector<cv::Point>{{55, 75}, {265, 255}, {1190, 470}, {610, 910}}) {
        cv::circle(fixture.image, dust, 2, cv::Scalar(240, 250, 240), cv::FILLED);
    }
    return fixture;
}

bool matchesCenters(
    const std::vector<cv::Point2d>& actual,
    const std::vector<cv::Point2d>& expected,
    const double tolerance) {
    if (actual.size() != expected.size()) {
        return false;
    }
    std::vector<bool> used(actual.size(), false);
    for (const auto& center : expected) {
        double best_distance = std::numeric_limits<double>::infinity();
        std::size_t best_index = actual.size();
        for (std::size_t index = 0; index < actual.size(); ++index) {
            if (used[index]) {
                continue;
            }
            const double distance = cv::norm(actual[index] - center);
            if (distance < best_distance) {
                best_distance = distance;
                best_index = index;
            }
        }
        if (best_index == actual.size() || best_distance > tolerance) {
            return false;
        }
        used[best_index] = true;
    }
    return true;
}

bool matchesExpectedCenters(
    const std::vector<SpotObservation>& observations,
    const std::vector<cv::Point2d>& expected,
    const double tolerance) {
    std::vector<cv::Point2d> actual;
    actual.reserve(observations.size());
    for (const auto& observation : observations) {
        actual.push_back(observation.center);
    }
    return matchesCenters(actual, expected, tolerance);
}

void verifySizeAndBrightnessVariation(
    TestContext& test,
    const std::filesystem::path& temp_root,
    const ProcessingConfig& config) {
    const GridFixture fixture = makeSizeAndBrightnessVariationFixture();
    const std::filesystem::path jpeg_path = temp_root / "size_brightness_grid_q92.jpg";
    test.expect(
        writeJpeg(jpeg_path, fixture.image, 92),
        "size and brightness variation JPEG fixture must be writable");

    const ImageProcessor processor;
    const ImageAnalysis analysis = processor.processFile(jpeg_path, config);
    if (!analysis.ok() ||
        !matchesExpectedCenters(analysis.observations, fixture.expected_centers, 5.0)) {
        std::cerr << "Size/brightness diagnostics: error=" << analysis.error.code
                  << ", count=" << analysis.observations.size()
                  << ", considered="
                  << analysis.diagnostics.lattice_recovery_considered_count
                  << ", signal_rejected="
                  << analysis.diagnostics.lattice_recovery_rejected_signal_count
                  << ", step_rejected="
                  << analysis.diagnostics.lattice_recovery_rejected_step_count
                  << ", geometry_rejected="
                  << analysis.diagnostics.lattice_recovery_rejected_geometry_count
                  << ", recovered=" << analysis.diagnostics.lattice_recovered_count << "\n";
    }
    test.expect(analysis.ok(), "size and brightness variation fixture must remain usable");
    test.expect(
        analysis.ok() &&
            matchesExpectedCenters(analysis.observations, fixture.expected_centers, 5.0),
        "all generated lattice spots must survive size, brightness, dark-core, and JPEG variation");
    test.expect(
        analysis.diagnostics.lattice_recovered_count > 0,
        "the variation fixture must exercise lattice-supported recovery rather than a relaxed global threshold");
}

struct LatticeRecoveryFixture {
    cv::Mat image;
    std::vector<cv::Point2d> expected_centers;
    std::vector<cv::Point2d> small_lattice_centers;
    cv::Point2d half_cell_distractor;
};

LatticeRecoveryFixture makeLatticeRecoveryFixture(
    const double rotation_degrees,
    const int pitch) {
    LatticeRecoveryFixture fixture;
    fixture.image = cv::Mat(900, 900, CV_8UC3, cv::Scalar(1, 2, 1));
    const cv::Point2d image_center(450.0, 450.0);
    const double radians = rotation_degrees * CV_PI / 180.0;
    const double cosine = std::cos(radians);
    const double sine = std::sin(radians);
    const auto rotate = [&](const cv::Point2d& point) {
        const cv::Point2d offset = point - image_center;
        return cv::Point2d(
            image_center.x + cosine * offset.x - sine * offset.y,
            image_center.y + sine * offset.x + cosine * offset.y);
    };

    constexpr int kGridSide = 5;
    const cv::Point2d origin(
        image_center.x - 2.0 * pitch,
        image_center.y - 2.0 * pitch);
    for (int row = 0; row < kGridSide; ++row) {
        for (int column = 0; column < kGridSide; ++column) {
            const cv::Point2d unrotated(
                origin.x + column * pitch,
                origin.y + row * pitch);
            const cv::Point2d center = rotate(unrotated);
            const cv::Point draw_center(cvRound(center.x), cvRound(center.y));
            const bool small_lattice_spot = row == 0 && column <= 1;
            cv::circle(
                fixture.image,
                draw_center,
                small_lattice_spot ? 4 : 9,
                cv::Scalar(5, 225, 4),
                cv::FILLED,
                cv::LINE_AA);
            fixture.expected_centers.emplace_back(draw_center);
            if (small_lattice_spot) {
                fixture.small_lattice_centers.emplace_back(draw_center);
            }
        }
    }

    const cv::Point2d half_cell =
        rotate(origin + cv::Point2d(0.5 * pitch, 0.5 * pitch));
    const cv::Point half_cell_draw_center(cvRound(half_cell.x), cvRound(half_cell.y));
    fixture.half_cell_distractor = half_cell_draw_center;
    cv::circle(
        fixture.image,
        half_cell_draw_center,
        4,
        cv::Scalar(5, 225, 4),
        cv::FILLED,
        cv::LINE_AA);
    return fixture;
}

void verifyLatticePhaseRecovery(
    TestContext& test,
    const std::filesystem::path& temp_root,
    const ProcessingConfig& config) {
    struct Scenario {
        double rotation_degrees;
        int pitch;
        int jpeg_quality;
    };
    const std::vector<Scenario> scenarios{{0.0, 120, 96}, {27.0, 105, 84}};
    const ImageProcessor processor;
    for (std::size_t index = 0; index < scenarios.size(); ++index) {
        const auto& scenario = scenarios[index];
        const LatticeRecoveryFixture fixture = makeLatticeRecoveryFixture(
            scenario.rotation_degrees, scenario.pitch);
        const std::filesystem::path jpeg_path =
            temp_root / ("lattice_phase_" + std::to_string(index) + ".jpg");
        test.expect(
            writeJpeg(jpeg_path, fixture.image, scenario.jpeg_quality),
            "lattice phase JPEG fixture must be writable");
        const ImageAnalysis analysis = processor.processFile(jpeg_path, config);
        test.expect(analysis.ok(), "lattice phase fixture must remain usable");
        test.expect(
            analysis.ok() &&
                matchesExpectedCenters(analysis.observations, fixture.expected_centers, 3.0),
            "a small true lattice spot must be retained while an equally bright half-cell distractor is rejected");
        test.expect(
            analysis.diagnostics.lattice_recovered_count ==
                    static_cast<int>(fixture.small_lattice_centers.size()) &&
                analysis.diagnostics.lattice_recovered_centers.size() ==
                    fixture.small_lattice_centers.size() &&
                matchesCenters(
                    analysis.diagnostics.lattice_recovered_centers,
                    fixture.small_lattice_centers,
                    3.0),
            "only the known adjacent small lattice members may enter the final output through iterative recovery");
        for (const auto& expected_recovered_center : fixture.small_lattice_centers) {
            const auto recovered = std::find_if(
                analysis.observations.begin(),
                analysis.observations.end(),
                [&expected_recovered_center](const SpotObservation& observation) {
                    return cv::norm(observation.center - expected_recovered_center) <= 3.0;
                });
            test.expect(
                recovered != analysis.observations.end() &&
                    std::find(
                        recovered->quality_flags.begin(),
                        recovered->quality_flags.end(),
                        "LATTICE_RECOVERED_UNVERIFIED") != recovered->quality_flags.end(),
                "each iteratively recovered point must remain visible to M3 as unverified");
        }
        test.expect(
            analysis.diagnostics.rejected_relative_area_count >= 1,
            "the half-cell distractor must reach relative-area screening and be rejected by lattice phase");
    }
}

cv::Mat makeSubpitchDeepValleyFixture() {
    cv::Mat image = makeCandidateFilterFixture(false, false, false, false);
    cv::circle(image, cv::Point(270, 365), 7, cv::Scalar(8, 230, 7), cv::FILLED, cv::LINE_AA);
    cv::circle(image, cv::Point(285, 365), 5, cv::Scalar(8, 230, 7), cv::FILLED, cv::LINE_AA);
    return image;
}

void verifyConservativeCandidateFiltering(TestContext& test) {
    ProcessingConfig config;
    config.recognition_mode = RecognitionMode::HartmannMultispotExperimental;
    config.roi_width_ratio = 1.0;
    config.roi_height_ratio = 1.0;
    const ImageProcessor processor;

    const ImageAnalysis edge = processor.processMat(
        makeCandidateFilterFixture(true, true, false, false), config);
    test.expect(edge.ok(), "edge fixture must remain usable after rejecting only the clipped spot");
    test.expect(
        edge.observations.size() == 13U,
        "a complete spot near the edge must be retained while the clipped spot is rejected");
    test.expect(
        edge.diagnostics.rejected_border_count == 1,
        "only the component intersecting the configured edge guard must be rejected");
    const auto contains_center = [](const ImageAnalysis& analysis, const cv::Point2d& expected) {
        return std::any_of(
            analysis.observations.begin(),
            analysis.observations.end(),
            [&expected](const SpotObservation& observation) {
                return cv::norm(observation.center - expected) <= 2.0;
            });
    };
    test.expect(
        contains_center(edge, cv::Point2d(12.0, 190.0)),
        "the complete near-edge spot must be the component that is retained");
    test.expect(
        !contains_center(edge, cv::Point2d(0.0, 205.0)),
        "the signal-connected clipped component must not appear in the final observations");

    const ImageAnalysis close_pair = processor.processMat(
        makeCandidateFilterFixture(false, false, true, false), config);
    test.expect(close_pair.ok(), "close-pair fixture must remain usable");
    test.expect(
        close_pair.observations.size() == 14U,
        "two similarly sized nearby spots must not be silently deduplicated");
    test.expect(
        close_pair.diagnostics.rejected_proximity_count == 0,
        "proximity suppression must require a clearly smaller fragment");

    const ImageAnalysis unequal_pair = processor.processMat(makeUnequalNearPairFixture(), config);
    if (!unequal_pair.ok() || unequal_pair.observations.size() != 14U ||
        unequal_pair.diagnostics.rejected_proximity_count != 0) {
        std::cerr << "Unequal near-pair diagnostics: count=" << unequal_pair.observations.size()
                  << ", proximity=" << unequal_pair.diagnostics.rejected_proximity_count
                  << ", error=" << unequal_pair.error.code << "\n";
    }
    test.expect(unequal_pair.ok(), "unequal near-pair fixture must remain usable");
    test.expect(
        unequal_pair.observations.size() == 14U &&
            unequal_pair.diagnostics.rejected_proximity_count == 0,
        "a smaller nearby spot separated by a short dark gap must not be treated as a fragment");

    const ImageAnalysis halo_pair =
        processor.processMat(makeUnequalNearPairWithContinuousHaloFixture(), config);
    test.expect(halo_pair.ok(), "unequal peaks with a continuous low-intensity halo must remain usable");
    test.expect(
        halo_pair.observations.size() == 14U &&
            halo_pair.diagnostics.rejected_proximity_count == 0,
        "a deep saddle between unequal peaks must prevent fragment suppression even when the halo is continuous");
    test.expect(
        contains_center(halo_pair, cv::Point2d(270.0, 365.0)) &&
            contains_center(halo_pair, cv::Point2d(294.0, 365.0)),
        "both independent halo-connected peak centers must remain in the output");
    test.expect(
        std::find(
            halo_pair.diagnostics.warnings.begin(),
            halo_pair.diagnostics.warnings.end(),
            "NEARBY_CANDIDATE_UNRESOLVED") != halo_pair.diagnostics.warnings.end(),
        "independent nearby peaks with a continuous halo must be reported as unresolved rather than deleted");

    const ImageAnalysis subpitch_pair =
        processor.processMat(makeSubpitchDeepValleyFixture(), config);
    test.expect(subpitch_pair.ok(), "sub-pitch fragment fixture must remain usable after filtering");
    test.expect(
        subpitch_pair.observations.size() == 13U &&
            subpitch_pair.diagnostics.rejected_proximity_count == 1,
        "a smaller candidate within 20 percent of the typical spacing must exercise the sub-pitch rule");
    test.expect(
        std::find(
            subpitch_pair.diagnostics.warnings.begin(),
            subpitch_pair.diagnostics.warnings.end(),
            "SUBPITCH_FRAGMENT_REJECTED_UNVERIFIED") != subpitch_pair.diagnostics.warnings.end(),
        "sub-pitch rejection without a bright bridge must disclose its unverified heuristic status");

    const ImageAnalysis diagonal_merge = processor.processMat(
        makeCandidateFilterFixture(false, false, false, true), config);
    test.expect(
        !diagonal_merge.ok() && diagonal_merge.error.code == "CENTROID_FAILED",
        "a diagonally merged component must be rejected independent of image axes");
    test.expect(
        std::find(
            diagonal_merge.diagnostics.warnings.begin(),
            diagonal_merge.diagnostics.warnings.end(),
            "POSSIBLE_MERGED_COMPONENT") != diagonal_merge.diagnostics.warnings.end(),
        "diagonal merge rejection must report the corresponding quality warning");
}

void verifyGreenJpegAndReencoding(
    TestContext& test,
    const std::filesystem::path& temp_root,
    const std::filesystem::path& synthetic_root,
    const ProcessingConfig& config) {
    const ImageProcessor processor;
    const ImageAnalysis baseline =
        processor.processFile(synthetic_root / "calibration/94_clean_reference.png", config);
    test.expect(
        baseline.ok() && baseline.observations.size() == 94U,
        "94-point grayscale baseline must pass before JPEG comparison");
    if (!baseline.ok() || baseline.observations.size() != 94U) {
        return;
    }

    const cv::Mat source = baseline.original;
    test.expect(!source.empty() && source.type() == CV_8UC1, "94-point source must decode as 8-bit grayscale");
    if (source.empty() || source.type() != CV_8UC1) {
        return;
    }

    const std::filesystem::path jpeg_path = temp_root / "green_94_q95.jpg";
    const std::filesystem::path reencoded_path = temp_root / "green_94_q95_reencoded.jpg";
    const std::filesystem::path gray_jpeg_path = temp_root / "gray_94_q95.jpg";
    const std::filesystem::path green_tiff_path = temp_root / "green_94.tif";
    const std::filesystem::path red_tiff_path = temp_root / "red_94.tif";
    test.expect(writeJpeg(jpeg_path, makeGreenBgr(source), 95), "green RGB JPEG fixture must be writable");
    const ImageAnalysis jpeg = processor.processFile(jpeg_path, config);
    const cv::Mat first_decode = jpeg.original;
    test.expect(
        !first_decode.empty() && first_decode.type() == CV_8UC3,
        "green JPEG must decode as 8-bit three-channel input");
    test.expect(writeJpeg(reencoded_path, first_decode, 95), "second-generation JPEG fixture must be writable");

    const ImageAnalysis reencoded = processor.processFile(reencoded_path, config);
    test.expect(writeJpeg(gray_jpeg_path, source, 95), "grayscale JPEG fixture must be writable");
    test.expect(writeTiff(green_tiff_path, makeGreenBgr(source)), "green RGB TIFF fixture must be writable");
    test.expect(writeTiff(red_tiff_path, makeRedBgr(source)), "red RGB TIFF fixture must be writable");
    const ImageAnalysis gray_jpeg = processor.processFile(gray_jpeg_path, config);
    const ImageAnalysis green_tiff = processor.processFile(green_tiff_path, config);
    const ImageAnalysis red_tiff = processor.processFile(red_tiff_path, config);
    test.expect(jpeg.ok() && jpeg.observations.size() == 94U, "green JPEG must retain 94 detections");
    test.expect(
        reencoded.ok() && reencoded.observations.size() == 94U,
        "re-encoded green JPEG must retain 94 detections");
    test.expect(
        gray_jpeg.ok() && gray_jpeg.observations.size() == 94U &&
            gray_jpeg.diagnostics.segmentation_source == "native_grayscale" &&
            gray_jpeg.diagnostics.centroid_intensity_source == "native_grayscale",
        "grayscale JPEG must retain the native grayscale 94-point path");
    test.expect(
        green_tiff.ok() && green_tiff.observations.size() == 94U &&
            green_tiff.diagnostics.channels == 3 &&
            green_tiff.diagnostics.segmentation_source == "bt601_luminance" &&
            green_tiff.diagnostics.centroid_intensity_source == "green_channel",
        "RGB TIFF must retain the color multispot path");
    test.expect(
        red_tiff.ok() && red_tiff.observations.size() == 94U &&
            red_tiff.diagnostics.centroid_intensity_source == "bt601_luminance_fallback" &&
            std::find(
                red_tiff.diagnostics.warnings.begin(),
                red_tiff.diagnostics.warnings.end(),
                "GREEN_CHANNEL_SIGNAL_WEAK") != red_tiff.diagnostics.warnings.end(),
        "non-green RGB TIFF must fall back to luminance centroid weighting");
    if (!jpeg.ok() || !reencoded.ok()) {
        return;
    }
    const MatchMetrics first_metrics = compareCenters(jpeg.observations, baseline.observations);
    const MatchMetrics second_metrics = compareCenters(reencoded.observations, baseline.observations);
    test.expect(
        first_metrics.complete && first_metrics.rms <= 0.6 && first_metrics.maximum <= 1.0,
        "first-generation JPEG coordinates must stay within the software bound");
    test.expect(
        second_metrics.complete && second_metrics.rms <= 1.0 && second_metrics.maximum <= 2.0,
        "second-generation JPEG coordinates must stay within the re-encoding bound");
    test.expect(
        jpeg.diagnostics.centroid_intensity_source == "green_channel" &&
            jpeg.diagnostics.segmentation_source == "bt601_luminance",
        "RGB JPEG must report luminance segmentation and green-channel centroid intensity");

    ProcessingConfig capped = config;
    capped.multispot_max_count = 20;
    const ImageAnalysis over_limit = processor.processMat(source, capped);
    test.expect(
        !over_limit.ok() && over_limit.error.code == "SPOT_COUNT_MISMATCH" &&
            over_limit.diagnostics.candidate_limit_exceeded &&
            over_limit.diagnostics.rejected_proximity_count == 0,
        "candidate count must be rejected before quadratic proximity filtering");

    ProcessingConfig invalid_fragment_ratio = config;
    invalid_fragment_ratio.multispot_fragment_max_area_ratio = 1.0;
    const ImageAnalysis invalid_config = processor.processMat(source, invalid_fragment_ratio);
    test.expect(
        !invalid_config.ok() && invalid_config.error.code == "CONFIG_INVALID",
        "fragment area ratio must be validated before detection");
}

void verifyDeterminismAndSerializer(
    TestContext& test,
    const std::filesystem::path& temp_root,
    const ProcessingConfig& config) {
    const std::filesystem::path jpeg_path = temp_root / "realistic_green_q90.jpg";
    test.expect(
        writeJpeg(jpeg_path, makeRealisticGreenFixture(), 90),
        "realistic green JPEG fixture must be writable");

    const ImageProcessor processor;
    std::vector<ImageAnalysis> runs;
    for (int attempt = 0; attempt < 3; ++attempt) {
        runs.push_back(processor.processFile(jpeg_path, config));
        if (runs.back().observations.size() != 21U ||
            runs.back().diagnostics.rejected_proximity_count < 1) {
            std::cerr << "JPEG fragment fixture diagnostics: count="
                      << runs.back().observations.size()
                      << ", raw=" << runs.back().diagnostics.raw_candidate_count
                      << ", area=" << runs.back().diagnostics.rejected_area_count
                      << ", border=" << runs.back().diagnostics.rejected_border_count
                      << ", shape=" << runs.back().diagnostics.rejected_shape_count
                      << ", proximity=" << runs.back().diagnostics.rejected_proximity_count
                      << "\n";
        }
        test.expect(runs.back().ok(), "realistic green JPEG run must succeed");
        test.expect(
            runs.back().observations.size() == 21U,
            "realistic green JPEG must keep 20 grid spots and one deduplicated fragmented spot");
        test.expect(
            runs.back().diagnostics.rejected_border_count >= 1,
            "realistic green JPEG must reject edge-clipped components without failing");
        test.expect(
            runs.back().diagnostics.rejected_area_count > 0,
            "realistic green JPEG must reject small JPEG/dust components by area");
        test.expect(
            runs.back().diagnostics.rejected_proximity_count >= 1,
            "realistic green JPEG must reject nearby fragments without duplicating a spot");
    }
    if (runs.size() != 3U || !runs.front().ok()) {
        return;
    }
    for (std::size_t run_index = 1; run_index < runs.size(); ++run_index) {
        test.expect(
            runs[run_index].observations.size() == runs.front().observations.size(),
            "repeated JPEG detections must retain the same count");
        if (runs[run_index].observations.size() != runs.front().observations.size()) {
            continue;
        }
        for (std::size_t index = 0; index < runs.front().observations.size(); ++index) {
            test.expect(
                sameObservation(runs.front().observations[index], runs[run_index].observations[index]),
                "repeated JPEG detections must retain identical sorted observations");
        }
    }

    InputPackage input;
    input.task_id = "m2_jpeg_determinism";
    input.data_source = "synthetic";
    std::vector<std::string> serialized;
    for (int attempt = 0; attempt < 3; ++attempt) {
        const std::filesystem::path output =
            temp_root / ("deterministic_" + std::to_string(attempt) + ".json");
        focimeter::m2::ErrorInfo write_error;
        test.expect(
            focimeter::m2::writeExperimentalMultispotSuccess(
                output, input, "calibration", runs[static_cast<std::size_t>(attempt)], config, write_error),
            "deterministic experimental JSON must be writable");
        const auto text = readText(output);
        test.expect(text.has_value(), "deterministic experimental JSON must be readable");
        if (text.has_value()) {
            serialized.push_back(*text);
        }
    }
    test.expect(
        serialized.size() == 3U && serialized[0] == serialized[1] && serialized[1] == serialized[2],
        "three repeated experimental spot documents must be byte-identical");
    if (serialized.empty()) {
        return;
    }
    const json document = json::parse(serialized.front());
    test.expect(
        document.at("schema_version") == "m2.multispot.experimental.1" &&
            document.at("data_source") == "synthetic" &&
            document.at("validation_status") == "software_verified" &&
            document.at("validation_scope") == "software_only" &&
            document.at("matching").at("status") == "not_performed" &&
            document.at("matching").at("id_scope") == "image_local" &&
            !document.at("matching").at("physical_identity_guaranteed").get<bool>() &&
            !document.at("metrology_validated").get<bool>(),
        "JPEG output must retain the experimental identity and validation boundary");
    test.expect(
        document.at("quality").at("rejected_proximity_count").get<int>() >= 1,
        "JPEG output must report the nearby fragment rejected by the detector");
    test.expect(
        document.at("quality").contains("rejected_absolute_area_count") &&
            document.at("quality").contains("rejected_relative_area_count") &&
        document.at("quality").contains("rejected_zero_signal_count") &&
            document.at("quality").contains("lattice_recovery_considered_count") &&
            document.at("quality").contains("lattice_recovery_rejected_signal_count") &&
            document.at("quality").contains("lattice_recovery_rejected_step_count") &&
            document.at("quality").contains("lattice_recovery_rejected_geometry_count") &&
            document.at("quality").contains("lattice_recovered_count"),
        "JPEG output must expose auditable area-rejection and lattice-recovery counts");
    test.expect(
        document.at("spots").size() == 21U &&
            document.at("quality").at("detected_count") == 21 &&
            document.at("quality").contains("rejected_shape_count") &&
            document.at("quality").at("segmentation_source") == "bt601_luminance" &&
            document.at("quality").at("centroid_intensity_source") == "green_channel",
        "JPEG JSON must preserve the detected count and channel diagnostics");
    for (std::size_t index = 0; index < document.at("spots").size(); ++index) {
        const auto& spot = document.at("spots").at(index);
        const double principal_axis_elongation =
            spot.at("principal_axis_elongation_ratio").get<double>();
        test.expect(
            spot.at("detection_id") == index && !spot.contains("spot_id") &&
                spot.contains("bounding_box_elongation_ratio") &&
                std::isfinite(principal_axis_elongation) && principal_axis_elongation >= 1.0,
            "JPEG output must use image-local IDs and expose both shape diagnostics");
    }
}

struct DistributionSummary {
    double minimum{0.0};
    double median{0.0};
    double maximum{0.0};
};

template <typename Selector>
DistributionSummary summarize(
    const std::vector<SpotObservation>& observations,
    Selector selector) {
    std::vector<double> values;
    values.reserve(observations.size());
    for (const auto& observation : observations) {
        values.push_back(selector(observation));
    }
    std::sort(values.begin(), values.end());
    return {values.front(), values[values.size() / 2U], values.back()};
}

void verifyRealJpegComponentDistribution(
    TestContext& test,
    const std::filesystem::path& real_root,
    const ProcessingConfig& config) {
    const ImageProcessor processor;
    const ImageAnalysis reference =
        processor.processFile(real_root / "images/reference_no_lens.jpg", config);
    const ImageAnalysis lens_001 =
        processor.processFile(real_root / "images/lens_001_spots.jpg", config);
    const ImageAnalysis lens_002 =
        processor.processFile(real_root / "images/lens_002_spots.jpg", config);
    test.expect(reference.ok() && lens_001.ok() && lens_002.ok(),
        "repository real JPEG fixtures must remain usable in experimental mode");
    if (!reference.ok() || !lens_001.ok() || !lens_002.ok()) {
        return;
    }
    test.expect(
        reference.observations.size() >= lens_001.observations.size() &&
            reference.observations.size() >= lens_002.observations.size(),
        "the shared no-lens reference must retain at least the complete candidates visible in either measurement");
    test.expect(
        reference.diagnostics.rejected_border_count > 0 &&
            lens_002.diagnostics.rejected_border_count > 0,
        "edge-clipped real candidates must still be rejected without fixing their exact count");
    test.expect(
        reference.diagnostics.lattice_recovered_count > 0,
        "the real reference must recover size-varying lattice candidates that the old area-only rule removed");
    test.expect(
        std::find(
            lens_001.diagnostics.warnings.begin(),
            lens_001.diagnostics.warnings.end(),
            "POSSIBLE_MERGED_COMPONENT") == lens_001.diagnostics.warnings.end(),
        "lens 001 must not be rejected solely because a large component is near-circular");

    const DistributionSummary area = summarize(
        lens_001.observations, [](const SpotObservation& observation) { return observation.area; });
    const DistributionSummary fill_ratio = summarize(
        lens_001.observations, [](const SpotObservation& observation) { return observation.circularity; });
    const DistributionSummary principal_axis = summarize(
        lens_001.observations,
        [](const SpotObservation& observation) { return observation.principal_axis_elongation; });
    test.expect(
        fill_ratio.minimum > 0.0 && fill_ratio.maximum <= 1.0 &&
            principal_axis.minimum >= 1.0 && area.minimum > 0.0,
        "real component shape and area diagnostics must stay finite and within their domains");
    std::cout << "Lens 001 component distribution: area="
              << area.minimum << "/" << area.median << "/" << area.maximum
              << ", fill_ratio=" << fill_ratio.minimum << "/" << fill_ratio.median << "/"
              << fill_ratio.maximum << ", principal_axis=" << principal_axis.minimum << "/"
              << principal_axis.median << "/" << principal_axis.maximum << "\n";
    std::cout << "Real JPEG counts: reference=" << reference.observations.size()
              << ", lens_001=" << lens_001.observations.size()
              << ", lens_002=" << lens_002.observations.size()
              << ", reference_lattice_recovered="
              << reference.diagnostics.lattice_recovered_count
              << ", reference_recovery_considered="
              << reference.diagnostics.lattice_recovery_considered_count
              << ", signal_rejected="
              << reference.diagnostics.lattice_recovery_rejected_signal_count
              << ", step_rejected="
              << reference.diagnostics.lattice_recovery_rejected_step_count
              << ", geometry_rejected="
              << reference.diagnostics.lattice_recovery_rejected_geometry_count << "\n";
    for (const auto& center : reference.diagnostics.lattice_rejected_step_centers) {
        std::cout << "Reference step-rejected candidate: x=" << center.x
                  << ", y=" << center.y << "\n";
    }
    for (const auto& center : reference.diagnostics.lattice_rejected_geometry_centers) {
        std::cout << "Reference geometry-rejected candidate: x=" << center.x
                  << ", y=" << center.y << "\n";
    }
}

}  // namespace

int runJpegTests(const std::vector<std::filesystem::path>& arguments) {
    std::filesystem::path temp_root;
    std::filesystem::path synthetic_root;
    std::filesystem::path real_root;
    for (std::size_t index = 1; index < arguments.size(); ++index) {
        const std::string argument = arguments[index].generic_string();
        if (argument == "--temp-dir" && index + 1 < arguments.size()) {
            temp_root = arguments[++index];
        } else if (argument == "--synthetic-root" && index + 1 < arguments.size()) {
            synthetic_root = arguments[++index];
        } else if (argument == "--real-root" && index + 1 < arguments.size()) {
            real_root = arguments[++index];
        } else {
            std::cerr << "Usage: m2_jpeg_tests --temp-dir <path> --synthetic-root <path> --real-root <path>\n";
            return 2;
        }
    }
    if (temp_root.empty() || synthetic_root.empty() || real_root.empty()) {
        std::cerr << "--temp-dir, --synthetic-root and --real-root are required.\n";
        return 2;
    }

    std::error_code filesystem_error;
    std::filesystem::create_directories(temp_root, filesystem_error);
    if (filesystem_error) {
        std::cerr << "Could not create JPEG test output directory.\n";
        return 2;
    }

    ProcessingConfig config;
    config.recognition_mode = RecognitionMode::HartmannMultispotExperimental;
    TestContext test;
    verifyGreenJpegAndReencoding(test, temp_root, synthetic_root, config);
    verifySizeAndBrightnessVariation(test, temp_root, config);
    verifyLatticePhaseRecovery(test, temp_root, config);
    verifyDeterminismAndSerializer(test, temp_root, config);
    verifyConservativeCandidateFiltering(test);
    verifyRealJpegComponentDistribution(test, real_root, config);

    std::filesystem::remove_all(temp_root, filesystem_error);
    if (filesystem_error) {
        test.expect(false, "JPEG test-owned output directory must be removable");
    }
    if (test.failures == 0) {
        std::cout << "All M2 JPEG tests passed.\n";
        return 0;
    }
    std::cerr << test.failures << " M2 JPEG test assertion(s) failed.\n";
    return 1;
}

#ifdef _WIN32
int wmain(int argc, wchar_t* argv[]) {
    std::vector<std::filesystem::path> arguments;
    arguments.reserve(static_cast<std::size_t>(argc));
    for (int index = 0; index < argc; ++index) {
        arguments.emplace_back(argv[index]);
    }
    return runJpegTests(arguments);
}
#else
int main(int argc, char* argv[]) {
    std::vector<std::filesystem::path> arguments;
    arguments.reserve(static_cast<std::size_t>(argc));
    for (int index = 0; index < argc; ++index) {
        arguments.emplace_back(std::filesystem::u8path(argv[index]));
    }
    return runJpegTests(arguments);
}
#endif
