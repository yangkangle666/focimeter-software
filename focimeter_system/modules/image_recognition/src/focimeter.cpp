#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

#include <opencv2/highgui.hpp>
#include <opencv2/imgcodecs.hpp>

#include "focimeter/m2/module.h"

namespace {

cv::Mat readImageForDisplay(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return {};
    }
    const std::vector<unsigned char> bytes{
        std::istreambuf_iterator<char>(stream),
        std::istreambuf_iterator<char>()};
    return bytes.empty() ? cv::Mat{} : cv::imdecode(bytes, cv::IMREAD_COLOR);
}

void printUsage() {
    std::cout
        << "Usage:\n"
        << "  focimeter_m2 --input <input_package.json> --output <output_dir> [options]\n\n"
        << "Options:\n"
        << "  --project-root <dir>     Explicit root used to resolve M1 relative paths.\n"
        << "  --save-intermediate       Save available processing images and diagnostic JSON.\n"
        << "  --show                    Display final annotations with OpenCV windows.\n"
        << "  --help                    Show this help.\n";
}

bool takeValue(
    const int argc,
    const std::vector<std::filesystem::path>& arguments,
    int& index,
    std::filesystem::path& destination,
    const char* option) {
    if (index + 1 >= argc) {
        std::cerr << "Missing value for " << option << ".\n";
        return false;
    }
    destination = arguments[static_cast<std::size_t>(++index)];
    return true;
}

int runCli(const std::vector<std::filesystem::path>& arguments) {
    const int argc = static_cast<int>(arguments.size());
    focimeter::m2::RunOptions options;
    bool show = false;
    if (argc == 1) {
        printUsage();
        return 2;
    }

    for (int index = 1; index < argc; ++index) {
        const std::string argument = arguments[static_cast<std::size_t>(index)].generic_string();
        if (argument == "--help") {
            printUsage();
            return 0;
        }
        if (argument == "--input") {
            if (!takeValue(argc, arguments, index, options.input_package, "--input")) {
                return 2;
            }
        } else if (argument == "--output") {
            if (!takeValue(argc, arguments, index, options.output_directory, "--output")) {
                return 2;
            }
        } else if (argument == "--project-root") {
            if (!takeValue(argc, arguments, index, options.project_root, "--project-root")) {
                return 2;
            }
        } else if (argument == "--save-intermediate") {
            options.save_intermediate = true;
        } else if (argument == "--show") {
            show = true;
            options.save_intermediate = true;
        } else {
            std::cerr << "Unknown option: " << argument << "\n";
            printUsage();
            return 2;
        }
    }

    if (options.input_package.empty() || options.output_directory.empty()) {
        std::cerr << "Both --input and --output are required.\n";
        printUsage();
        return 2;
    }

    const focimeter::m2::ImageRecognitionModule module;
    const focimeter::m2::RunResult result = module.run(options);
    if (result.ok()) {
        if (show) {
            try {
                const auto intermediate = options.output_directory / "intermediate";
                const cv::Mat calibration = readImageForDisplay(
                    intermediate / "calibration_spots.png");
                const cv::Mat measurement = readImageForDisplay(
                    intermediate / "measurement_spots.png");
                if (calibration.empty() || measurement.empty()) {
                    std::cerr << "M2 succeeded; warning: --show could not load the saved annotations.\n";
                } else {
                    cv::imshow("M2 calibration spots", calibration);
                    cv::imshow("M2 measurement spots", measurement);
                    cv::waitKey(0);
                    cv::destroyAllWindows();
                }
            } catch (const cv::Exception& exception) {
                std::cerr << "M2 succeeded; warning: --show failed: " << exception.what() << "\n";
            }
        }
        std::cout << "M2 completed successfully.\n"
                  << "  calibration: " << result.calibration_output.generic_string() << "\n"
                  << "  measurement: " << result.measurement_output.generic_string() << "\n"
                  << "  log: " << result.log_output.generic_string() << "\n";
        return 0;
    }

    std::cerr << "M2 failed";
    if (!result.error.code.empty()) {
        std::cerr << " [" << result.error.code << "]";
    }
    std::cerr << ": " << result.error.message << "\n";
    return result.exit_code == 0 ? 1 : result.exit_code;
}

}  // namespace

#ifdef _WIN32
int wmain(int argc, wchar_t* argv[]) {
    std::vector<std::filesystem::path> arguments;
    arguments.reserve(static_cast<std::size_t>(argc));
    for (int index = 0; index < argc; ++index) {
        arguments.emplace_back(argv[index]);
    }
    return runCli(arguments);
}
#else
int main(int argc, char* argv[]) {
    std::vector<std::filesystem::path> arguments;
    arguments.reserve(static_cast<std::size_t>(argc));
    for (int index = 0; index < argc; ++index) {
        arguments.emplace_back(std::filesystem::u8path(argv[index]));
    }
    return runCli(arguments);
}
#endif
