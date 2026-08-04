#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

namespace {

constexpr double kPi = 3.14159265358979323846;
using PointList = std::vector<cv::Point2d>;

PointList basePoints() {
    return {
        {512.0, 384.0},  // 0 center
        {512.0, 304.0},  // 1 y_positive
        {432.0, 384.0},  // 2 left_or_negative
        {512.0, 464.0},  // 3 other
        {592.0, 384.0},  // 4 x_positive
    };
}

PointList transform(
    const PointList& points,
    const double scale,
    const double degrees,
    const cv::Point2d& translation) {
    const cv::Point2d pivot(512.0, 384.0);
    const double radians = degrees * kPi / 180.0;
    const double cosine = std::cos(radians);
    const double sine = std::sin(radians);
    PointList result;
    result.reserve(points.size());
    for (const auto& point : points) {
        const cv::Point2d local = point - pivot;
        result.emplace_back(
            pivot.x + scale * (cosine * local.x - sine * local.y) + translation.x,
            pivot.y + scale * (sine * local.x + cosine * local.y) + translation.y);
    }
    return result;
}

PointList transformAnisotropic(
    const PointList& points,
    const double x_scale,
    const double y_scale,
    const cv::Point2d& translation) {
    const cv::Point2d pivot(512.0, 384.0);
    PointList result;
    result.reserve(points.size());
    for (const auto& point : points) {
        const cv::Point2d local = point - pivot;
        result.emplace_back(
            pivot.x + x_scale * local.x + translation.x,
            pivot.y + y_scale * local.y + translation.y);
    }
    return result;
}

cv::Mat render(
    const PointList& points,
    const int brightness = 235,
    const double noise_sigma = 0.0,
    const int background = 5,
    const int radius = 14) {
    cv::Mat image(768, 1024, CV_8UC3, cv::Scalar(background, background, background));
    for (const auto& point : points) {
        cv::circle(
            image,
            cv::Point(cvRound(point.x), cvRound(point.y)),
            radius,
            cv::Scalar(brightness, brightness, brightness),
            cv::FILLED,
            cv::LINE_AA);
    }
    if (noise_sigma > 0.0) {
        cv::Mat coarse_noise(96, 128, CV_16SC1);
        cv::RNG rng(20260722);
        rng.fill(coarse_noise, cv::RNG::NORMAL, 0.0, noise_sigma);
        cv::Mat noise_gray;
        cv::resize(coarse_noise, noise_gray, image.size(), 0.0, 0.0, cv::INTER_CUBIC);
        std::vector<cv::Mat> noise_channels(3, noise_gray);
        cv::Mat noise;
        cv::merge(noise_channels, noise);
        cv::Mat signed_image;
        image.convertTo(signed_image, CV_16SC3);
        signed_image += noise;
        signed_image.convertTo(image, CV_8UC3);
    }
    return image;
}

cv::Mat renderGradient(const PointList& points) {
    cv::Mat image(768, 1024, CV_8UC3);
    for (int row = 0; row < image.rows; ++row) {
        const int background = 5 + (35 * row) / (image.rows - 1);
        image.row(row).setTo(cv::Scalar(background, background, background));
    }
    for (const auto& point : points) {
        cv::circle(
            image,
            cv::Point(cvRound(point.x), cvRound(point.y)),
            14,
            cv::Scalar(215, 215, 215),
            cv::FILLED,
            cv::LINE_AA);
    }
    return image;
}

cv::Mat renderUnevenSpots(const PointList& points) {
    const std::array<int, 5> brightness{235, 220, 205, 225, 210};
    const std::array<int, 5> radii{14, 12, 13, 14, 13};
    cv::Mat image(768, 1024, CV_8UC3, cv::Scalar(8, 8, 8));
    for (std::size_t index = 0; index < points.size(); ++index) {
        cv::circle(
            image,
            cv::Point(cvRound(points[index].x), cvRound(points[index].y)),
            radii[index],
            cv::Scalar(brightness[index], brightness[index], brightness[index]),
            cv::FILLED,
            cv::LINE_AA);
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
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << text;
    return output.good();
}

}  // namespace

int runGenerator(const std::vector<std::filesystem::path>& arguments) {
    const int argc = static_cast<int>(arguments.size());
    std::filesystem::path output;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = arguments[static_cast<std::size_t>(index)].generic_string();
        if (argument == "--output" && index + 1 < argc) {
            output = arguments[static_cast<std::size_t>(++index)];
        } else if (argument == "--help") {
            std::cout << "Usage: m2_generate_synthetic --output <synthetic_directory>\n";
            return 0;
        } else {
            std::cerr << "Unknown or incomplete argument: " << argument << "\n";
            return 2;
        }
    }
    if (output.empty()) {
        std::cerr << "--output is required.\n";
        return 2;
    }

    const PointList base = basePoints();
    const PointList transformed = transform(base, 1.05, 8.0, {12.0, -8.0});
    const PointList rotated = transform(base, 1.0, 25.0, {4.0, -3.0});
    const PointList scaled = transform(base, 1.18, 0.0, {-8.0, 5.0});
    const PointList anisotropic = transformAnisotropic(base, 1.20, 0.80, {9.0, -6.0});
    bool ok = true;
    ok = writePng(output / "calibration" / "base_5spots.png", render(base)) && ok;
    ok = writePng(output / "measurement" / "translate_rotate_scale.png", render(transformed)) && ok;
    ok = writePng(output / "measurement" / "rotation_25deg.png", render(rotated)) && ok;
    ok = writePng(output / "measurement" / "scale_118pct.png", render(scaled)) && ok;
    ok = writePng(output / "measurement" / "anisotropic_xy.png", render(anisotropic)) && ok;
    ok = writePng(output / "measurement" / "brightness_only.png", render(transform(base, 1.0, 0.0, {3.0, -4.0}), 155)) && ok;
    ok = writePng(output / "measurement" / "noise_only.png", render(transform(base, 1.0, 0.0, {-4.0, 3.0}), 235, 7.0)) && ok;
    ok = writePng(output / "measurement" / "brightness_noise.png", render(transform(base, 0.98, -6.0, {-5.0, 9.0}), 195, 7.0)) && ok;
    ok = writePng(output / "measurement" / "low_contrast.png", render(base, 155, 0.0, 60)) && ok;
    cv::Mat blurred;
    cv::GaussianBlur(render(base), blurred, cv::Size(7, 7), 1.5);
    ok = writePng(output / "measurement" / "gaussian_blur.png", blurred) && ok;
    ok = writePng(output / "measurement" / "background_gradient.png", renderGradient(base)) && ok;
    ok = writePng(output / "measurement" / "uneven_spots.png", renderUnevenSpots(base)) && ok;

    PointList missing{base.begin(), base.begin() + 3};
    ok = writePng(output / "failure" / "missing_spots_3.png", render(missing)) && ok;
    PointList extra = base;
    extra.emplace_back(680.0, 510.0);
    ok = writePng(output / "failure" / "extra_spot_6.png", render(extra)) && ok;
    cv::Mat merged = render(base);
    cv::circle(merged, cv::Point(485, 384), 22, cv::Scalar(235, 235, 235), cv::FILLED, cv::LINE_AA);
    ok = writePng(output / "failure" / "merged_spots.png", merged) && ok;
    PointList ambiguous{{512.0, 384.0}, {512.0, 304.0}, {590.0, 350.0}, {590.0, 418.0}, {512.0, 464.0}};
    ok = writePng(output / "failure" / "ambiguous_roles.png", render(ambiguous)) && ok;
    ok = writePng(
        output / "failure" / "ambiguous_pairing_45deg.png",
        render(transform(base, 1.0, 45.0, {0.0, 0.0}))) && ok;
    ok = writePng(
        output / "failure" / "blank_dark.png",
        cv::Mat::zeros(768, 1024, CV_8UC1)) && ok;
    ok = writePng(
        output / "failure" / "blank_bright.png",
        cv::Mat(768, 1024, CV_8UC1, cv::Scalar(255))) && ok;
    PointList array_points;
    for (int row = 0; row < 5; ++row) {
        for (int column = 0; column < 5; ++column) {
            array_points.emplace_back(352.0 + column * 80.0, 224.0 + row * 80.0);
        }
    }
    ok = writePng(output / "failure" / "hartmann_array_25.png", render(array_points)) && ok;
    PointList boundary = base;
    boundary[1] = {512.0, 35.0};
    ok = writePng(output / "failure" / "roi_boundary_clipped.png", render(boundary)) && ok;

    const std::string manifest = R"({
  "schema_version": "1.0",
  "task_id": "m2_synthetic_dataset",
  "module": "m2_image_recognition",
  "status": "ok",
  "kind": "synthetic_mock",
  "validation_status": "synthetic_verified",
  "metrology_validated": false,
  "image_size": {"width": 1024, "height": 768},
  "calibration_spots": [
    {"spot_id": 0, "role": "center", "x": 512.0, "y": 384.0},
    {"spot_id": 1, "role": "y_positive", "x": 512.0, "y": 304.0},
    {"spot_id": 2, "role": "left_or_negative", "x": 432.0, "y": 384.0},
    {"spot_id": 3, "role": "other", "x": 512.0, "y": 464.0},
    {"spot_id": 4, "role": "x_positive", "x": 592.0, "y": 384.0}
  ],
  "cases": {
    "measurement/translate_rotate_scale.png": {"expected": "ok", "scale": 1.05, "rotation_degrees": 8.0, "translation": [12.0, -8.0]},
    "measurement/rotation_25deg.png": {"expected": "ok", "scale": 1.0, "rotation_degrees": 25.0, "translation": [4.0, -3.0]},
    "measurement/scale_118pct.png": {"expected": "ok", "scale": 1.18, "rotation_degrees": 0.0, "translation": [-8.0, 5.0]},
    "measurement/anisotropic_xy.png": {"expected": "ok", "affine_matrix": [[1.20, 0.0], [0.0, 0.80]], "translation": [9.0, -6.0]},
    "measurement/brightness_only.png": {"expected": "ok", "scale": 1.0, "rotation_degrees": 0.0, "translation": [3.0, -4.0], "brightness": 155},
    "measurement/noise_only.png": {"expected": "ok", "scale": 1.0, "rotation_degrees": 0.0, "translation": [-4.0, 3.0], "noise_sigma": 7.0},
    "measurement/brightness_noise.png": {"expected": "ok", "scale": 0.98, "rotation_degrees": -6.0, "translation": [-5.0, 9.0], "brightness": 195, "noise_sigma": 7.0},
    "measurement/low_contrast.png": {"expected": "ok", "scale": 1.0, "rotation_degrees": 0.0, "translation": [0.0, 0.0], "background": 60, "brightness": 155},
    "measurement/gaussian_blur.png": {"expected": "ok", "scale": 1.0, "rotation_degrees": 0.0, "translation": [0.0, 0.0], "gaussian_kernel": 7, "gaussian_sigma": 1.5},
    "measurement/background_gradient.png": {"expected": "ok", "scale": 1.0, "rotation_degrees": 0.0, "translation": [0.0, 0.0], "background_range": [5, 40]},
    "measurement/uneven_spots.png": {"expected": "ok", "scale": 1.0, "rotation_degrees": 0.0, "translation": [0.0, 0.0], "brightness_range": [205, 235], "radius_range": [12, 14]},
    "failure/missing_spots_3.png": {"expected": "SPOT_COUNT_MISMATCH"},
    "failure/extra_spot_6.png": {"expected": "SPOT_COUNT_MISMATCH"},
    "failure/merged_spots.png": {"expected": "CENTROID_FAILED"},
    "failure/ambiguous_roles.png": {"expected": "COORDINATE_SYSTEM_INVALID"},
    "failure/ambiguous_pairing_45deg.png": {"expected": "COORDINATE_SYSTEM_INVALID", "reason": "symmetric cross has multiple plausible identities"},
    "failure/blank_dark.png": {"expected": "SPOT_COUNT_MISMATCH", "expected_warning": "IMAGE_UNDEREXPOSED"},
    "failure/blank_bright.png": {"expected": "SPOT_COUNT_MISMATCH", "expected_warning": "IMAGE_OVEREXPOSED"},
    "failure/hartmann_array_25.png": {"expected": "SPOT_COUNT_MISMATCH", "expected_warning": "POSSIBLE_HARTMANN_ARRAY_INPUT"},
    "failure/roi_boundary_clipped.png": {"expected": "SPOT_COUNT_MISMATCH", "reason": "one spot lies outside the configured ROI"}
  },
  "error": null
}
)";
    ok = writeText(output / "manifest.json", manifest) && ok;
    if (!ok) {
        std::cerr << "Synthetic data generation failed.\n";
        return 1;
    }
    std::cout << "Synthetic data written to " << output.generic_string() << "\n";
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
