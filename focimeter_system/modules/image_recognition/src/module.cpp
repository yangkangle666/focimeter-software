#include "focimeter/m2/module.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <exception>
#include <fstream>
#include <optional>
#include <sstream>
#include <system_error>
#include <vector>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include "focimeter/m2/image_processor.h"
#include "focimeter/m2/json_io.h"
#include "focimeter/m2/output_lock.h"
#include "focimeter/m2/spot_matcher.h"

namespace focimeter::m2 {
namespace {

ErrorInfo makeError(std::string code, std::string message, const bool recoverable) {
    ErrorInfo error;
    error.code = std::move(code);
    error.message = std::move(message);
    error.recoverable = recoverable;
    return error;
}

struct RecoveryContext {
    std::optional<InputPackage> input;
    bool output_cleanup_is_safe{false};
};

bool validateProjectRelativePath(
    const std::filesystem::path& path,
    const std::string& field,
    ErrorInfo& error) {
    if (path.empty() || path.is_absolute() || path.has_root_name() || path.has_root_directory()) {
        error = makeError("UNKNOWN_ERROR", "Project input paths must be relative paths.", true);
        error.string_details["field"] = field;
        return false;
    }
    for (const auto& component : path) {
        if (component == "..") {
            error = makeError("UNKNOWN_ERROR", "Project input paths must not contain parent traversal.", true);
            error.string_details["field"] = field;
            return false;
        }
    }
    return true;
}

std::optional<std::filesystem::path> resolveInsideProject(
    const std::filesystem::path& project_root,
    const std::filesystem::path& relative_path,
    const std::string& field,
    ErrorInfo& error) {
    if (!validateProjectRelativePath(relative_path, field, error)) {
        return std::nullopt;
    }

    std::error_code path_error;
    const auto root = std::filesystem::weakly_canonical(project_root, path_error);
    if (path_error) {
        error = makeError("UNKNOWN_ERROR", "Could not resolve the project root.", false);
        return std::nullopt;
    }
    const auto candidate = std::filesystem::weakly_canonical(root / relative_path, path_error);
    if (path_error) {
        error = makeError("UNKNOWN_ERROR", "Could not resolve a project input path.", true);
        error.string_details["field"] = field;
        return std::nullopt;
    }
    const auto relation = candidate.lexically_relative(root);
    const std::string relation_text = relation.generic_string();
    if (relation.empty() || relation_text == ".." || relation_text.rfind("../", 0) == 0) {
        error = makeError("UNKNOWN_ERROR", "Input path escapes the configured project root.", true);
        error.string_details["field"] = field;
        return std::nullopt;
    }
    return candidate;
}

std::filesystem::path discoverProjectRoot(
    const RunOptions& options,
    const InputPackage& input) {
    if (!options.project_root.empty()) {
        return options.project_root;
    }

    auto candidate = options.input_package.parent_path();
    while (!candidate.empty()) {
        std::error_code file_error;
        if (std::filesystem::is_regular_file(candidate / input.config_path, file_error)) {
            return candidate;
        }
        const auto parent = candidate.parent_path();
        if (parent == candidate) {
            break;
        }
        candidate = parent;
    }
    return options.input_package.parent_path();
}

bool writeImage(const std::filesystem::path& path, const cv::Mat& image, ErrorInfo& error) {
    if (image.empty()) {
        return true;
    }
    std::vector<unsigned char> encoded;
    if (!cv::imencode(".png", image, encoded)) {
        error = makeError("UNKNOWN_ERROR", "OpenCV could not encode an intermediate PNG.", false);
        error.string_details["output_file"] = path.filename().generic_string();
        return false;
    }
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) {
        error = makeError("UNKNOWN_ERROR", "Could not write an intermediate PNG.", false);
        error.string_details["output_file"] = path.filename().generic_string();
        return false;
    }
    stream.write(reinterpret_cast<const char*>(encoded.data()), static_cast<std::streamsize>(encoded.size()));
    stream.flush();
    if (!stream.good()) {
        error = makeError("UNKNOWN_ERROR", "Writing an intermediate PNG failed.", false);
        error.string_details["output_file"] = path.filename().generic_string();
        return false;
    }
    stream.close();
    if (stream.fail()) {
        error = makeError("UNKNOWN_ERROR", "Closing an intermediate PNG failed.", false);
        error.string_details["output_file"] = path.filename().generic_string();
        return false;
    }
    return true;
}

cv::Mat annotateSpots(const ImageAnalysis& analysis, const std::vector<Spot>& spots) {
    cv::Mat annotated;
    if (analysis.original.channels() == 1) {
        cv::cvtColor(analysis.original, annotated, cv::COLOR_GRAY2BGR);
    } else if (analysis.original.channels() == 4) {
        cv::cvtColor(analysis.original, annotated, cv::COLOR_BGRA2BGR);
    } else {
        annotated = analysis.original.clone();
    }
    cv::rectangle(annotated, analysis.roi_rect, cv::Scalar(255, 160, 0), 1, cv::LINE_AA);
    for (const auto& spot : spots) {
        const cv::Point point(cvRound(spot.center.x), cvRound(spot.center.y));
        cv::circle(annotated, point, 10, cv::Scalar(0, 255, 0), 2, cv::LINE_AA);
        cv::putText(
            annotated,
            std::to_string(spot.spot_id) + " " + spot.role,
            point + cv::Point(10, -10),
            cv::FONT_HERSHEY_SIMPLEX,
            0.42,
            cv::Scalar(0, 255, 0),
            1,
            cv::LINE_AA);
    }
    return annotated;
}

std::vector<Spot> makeExperimentalDisplaySpots(const ImageAnalysis& analysis) {
    std::vector<Spot> spots;
    spots.reserve(analysis.observations.size());
    for (std::size_t index = 0; index < analysis.observations.size(); ++index) {
        const auto& observation = analysis.observations[index];
        spots.push_back({
            static_cast<int>(index),
            "detection",
            observation.center,
            observation.confidence,
        });
    }
    return spots;
}

bool saveArtifacts(
    const std::filesystem::path& directory,
    const std::string& task_id,
    const std::string& prefix,
    const ImageAnalysis& analysis,
    const std::vector<Spot>& spots,
    ErrorInfo& error) {
    std::error_code directory_error;
    std::filesystem::create_directories(directory, directory_error);
    if (directory_error) {
        error = makeError("UNKNOWN_ERROR", "Could not create intermediate-image directory.", false);
        return false;
    }
    const cv::Mat annotated = spots.empty() ? analysis.annotated : annotateSpots(analysis, spots);
    return writeImage(directory / (prefix + "_gray.png"), analysis.gray, error) &&
           writeImage(directory / (prefix + "_enhanced.png"), analysis.enhanced, error) &&
           writeImage(directory / (prefix + "_binary.png"), analysis.binary, error) &&
           writeImage(directory / (prefix + "_spots.png"), annotated, error) &&
           writeImageDiagnostics(
               directory / (prefix + "_diagnostics.json"), task_id, prefix, analysis, error);
}

void replaceImagePathDetail(ErrorInfo& error, const std::filesystem::path& relative_path) {
    const auto iterator = error.string_details.find("image_path");
    if (iterator != error.string_details.end()) {
        iterator->second = relative_path.generic_string();
    }
}

RunResult makeRunResult(const RunOptions& options) {
    RunResult result;
    result.recognition_mode = options.recognition_mode;
    if (options.recognition_mode == RecognitionMode::HartmannMultispotExperimental) {
        const auto experimental_directory = options.output_directory / "experimental_multispot";
        result.calibration_output = experimental_directory / "spots_calib_multispot.json";
        result.measurement_output = experimental_directory / "spots_meas_multispot.json";
        result.log_output = experimental_directory / "m2_multispot_run_log.json";
    } else {
        result.calibration_output = options.output_directory / "spots_calib.json";
        result.measurement_output = options.output_directory / "spots_meas.json";
        result.log_output = options.output_directory / "m2_run_log.json";
    }
    return result;
}

std::filesystem::path pendingPath(const std::filesystem::path& final_path) {
    auto pending = final_path;
    pending += ".m2-pending";
    return pending;
}

std::vector<std::filesystem::path> intermediateOutputPaths(
    const std::filesystem::path& output_directory);

std::filesystem::path managedOutputRoot(const RunResult& result) {
    const auto result_directory = result.calibration_output.parent_path();
    return result.recognition_mode == RecognitionMode::HartmannMultispotExperimental
        ? result_directory.parent_path()
        : result_directory;
}

std::vector<std::filesystem::path> managedOutputPaths(const RunResult& result) {
    std::vector<std::filesystem::path> paths{
        result.calibration_output,
        result.measurement_output,
        result.log_output,
        result.calibration_output.parent_path() / "m2_error.json",
        managedOutputRoot(result) / ".focimeter_m2.lock",
        pendingPath(result.calibration_output),
        pendingPath(result.measurement_output),
        pendingPath(result.log_output),
    };
    const auto intermediate = intermediateOutputPaths(result.calibration_output.parent_path());
    paths.insert(paths.end(), intermediate.begin(), intermediate.end());
    return paths;
}

std::filesystem::path normalizedAbsolutePath(const std::filesystem::path& path) {
    std::error_code path_error;
    auto normalized = std::filesystem::weakly_canonical(path, path_error);
    if (path_error) {
        path_error.clear();
        normalized = std::filesystem::absolute(path, path_error).lexically_normal();
    }
    return path_error ? path.lexically_normal() : normalized;
}

std::filesystem::path lexicalAbsolutePath(const std::filesystem::path& path) {
    std::error_code path_error;
    const auto absolute = std::filesystem::absolute(path, path_error);
    return (path_error ? path : absolute).lexically_normal();
}

std::string comparablePathText(const std::filesystem::path& path) {
    std::string text = path.generic_string();
#ifdef _WIN32
    std::transform(text.begin(), text.end(), text.begin(), [](const unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
#endif
    return text;
}

bool pathsLexicallyReferToSameLocation(
    const std::filesystem::path& left,
    const std::filesystem::path& right) {
    return comparablePathText(lexicalAbsolutePath(left)) ==
        comparablePathText(lexicalAbsolutePath(right));
}

bool pathsReferToSameLocation(
    const std::filesystem::path& left,
    const std::filesystem::path& right) {
    std::error_code left_exists_error;
    std::error_code right_exists_error;
    std::error_code equivalent_error;
    if (std::filesystem::exists(left, left_exists_error) &&
        std::filesystem::exists(right, right_exists_error) &&
        !left_exists_error && !right_exists_error &&
        std::filesystem::equivalent(left, right, equivalent_error) && !equivalent_error) {
        return true;
    }
    return comparablePathText(normalizedAbsolutePath(left)) ==
        comparablePathText(normalizedAbsolutePath(right));
}

bool rejectInputOutputAliases(
    const RunResult& result,
    const std::vector<std::pair<std::string, std::filesystem::path>>& inputs,
    ErrorInfo& error) {
    const auto outputs = managedOutputPaths(result);
    for (const auto& [field, input] : inputs) {
        for (const auto& output : outputs) {
            if (pathsReferToSameLocation(input, output)) {
                error = makeError(
                    "UNKNOWN_ERROR",
                    "An input file must not overlap an M2-managed output file.",
                    false);
                error.string_details["field"] = field;
                error.string_details["output_file"] = output.filename().generic_string();
                return false;
            }
        }
    }
    return true;
}

bool rejectRawInputOutputAliases(
    const RunResult& result,
    const RunOptions& options,
    const InputPackage& input,
    ErrorInfo& error) {
    std::vector<std::filesystem::path> possible_roots;
    if (!options.project_root.empty()) {
        possible_roots.push_back(options.project_root);
    } else {
        auto candidate = options.input_package.parent_path();
        while (!candidate.empty()) {
            possible_roots.push_back(candidate);
            const auto parent = candidate.parent_path();
            if (parent == candidate) {
                break;
            }
            candidate = parent;
        }
    }

    const std::array<std::pair<std::string, std::filesystem::path>, 3> declared_inputs{{
        {"data.config_path", input.config_path},
        {"data.calibration_image", input.calibration_image},
        {"data.measurement_image", input.measurement_image},
    }};
    const auto outputs = managedOutputPaths(result);
    for (const auto& [field, declared] : declared_inputs) {
        std::vector<std::filesystem::path> candidates;
        if (declared.is_absolute() || declared.has_root_name() || declared.has_root_directory()) {
            candidates.push_back(declared);
        } else {
            for (const auto& root : possible_roots) {
                candidates.push_back(root / declared);
            }
        }
        for (const auto& candidate : candidates) {
            for (const auto& output : outputs) {
                if (pathsLexicallyReferToSameLocation(candidate, output)) {
                    error = makeError(
                        "UNKNOWN_ERROR",
                        "An input file must not overlap an M2-managed output file.",
                        false);
                    error.string_details["field"] = field;
                    error.string_details["output_file"] = output.filename().generic_string();
                    return false;
                }
            }
        }
    }
    return true;
}

bool removeKnownFile(const std::filesystem::path& path, ErrorInfo& error) {
    std::error_code remove_error;
    std::filesystem::remove(path, remove_error);
    if (remove_error) {
        error = makeError("UNKNOWN_ERROR", "Could not invalidate a previous M2 output.", false);
        error.string_details["output_file"] = path.filename().generic_string();
        return false;
    }
    return true;
}

std::vector<std::filesystem::path> intermediateOutputPaths(
    const std::filesystem::path& output_directory) {
    const auto directory = output_directory / "intermediate";
    return {
        directory / "calibration_gray.png",
        directory / "calibration_enhanced.png",
        directory / "calibration_binary.png",
        directory / "calibration_spots.png",
        directory / "calibration_diagnostics.json",
        directory / "measurement_gray.png",
        directory / "measurement_enhanced.png",
        directory / "measurement_binary.png",
        directory / "measurement_spots.png",
        directory / "measurement_diagnostics.json",
    };
}

bool removeIntermediateOutputs(const RunResult& result, ErrorInfo& error) {
    for (const auto& path : intermediateOutputPaths(result.calibration_output.parent_path())) {
        if (!removeKnownFile(path, error)) {
            return false;
        }
    }
    return true;
}

bool prepareOutputTargets(const RunResult& result, ErrorInfo& error) {
    if (result.calibration_output.parent_path().empty()) {
        error = makeError("UNKNOWN_ERROR", "Output directory must not be empty.", false);
        return false;
    }
    std::error_code directory_error;
    std::filesystem::create_directories(result.calibration_output.parent_path(), directory_error);
    if (directory_error) {
        error = makeError("UNKNOWN_ERROR", "Could not create output directory.", false);
        return false;
    }

    const std::array<std::filesystem::path, 7> known_outputs{
        result.calibration_output,
        result.measurement_output,
        result.log_output,
        result.calibration_output.parent_path() / "m2_error.json",
        pendingPath(result.calibration_output),
        pendingPath(result.measurement_output),
        pendingPath(result.log_output),
    };
    for (const auto& path : known_outputs) {
        if (!removeKnownFile(path, error)) {
            return false;
        }
    }
    return removeIntermediateOutputs(result, error);
}

bool publishPair(
    const std::filesystem::path& calibration_pending,
    const std::filesystem::path& measurement_pending,
    const RunResult& result,
    ErrorInfo& error) {
    std::error_code rename_error;
    std::filesystem::rename(calibration_pending, result.calibration_output, rename_error);
    if (rename_error) {
        error = makeError("UNKNOWN_ERROR", "Could not publish calibration JSON output.", false);
        error.string_details["output_file"] = result.calibration_output.filename().generic_string();
        std::error_code ignored;
        std::filesystem::remove(calibration_pending, ignored);
        std::filesystem::remove(measurement_pending, ignored);
        return false;
    }

    std::filesystem::rename(measurement_pending, result.measurement_output, rename_error);
    if (rename_error) {
        error = makeError("UNKNOWN_ERROR", "Could not publish measurement JSON output.", false);
        error.string_details["output_file"] = result.measurement_output.filename().generic_string();
        std::error_code ignored;
        // Roll back the first rename when the second rename reports a normal failure.
        std::filesystem::remove(result.calibration_output, ignored);
        std::filesystem::remove(measurement_pending, ignored);
        return false;
    }
    return true;
}

bool stageErrorPair(
    const RunResult& result,
    const InputPackage& input,
    const ErrorInfo& calibration_error,
    const ErrorInfo& measurement_error,
    ErrorInfo& write_error) {
    const auto calibration_pending = pendingPath(result.calibration_output);
    const auto measurement_pending = pendingPath(result.measurement_output);
    const bool experimental =
        result.recognition_mode == RecognitionMode::HartmannMultispotExperimental;
    ErrorInfo calibration_write_error;
    ErrorInfo measurement_write_error;
    const bool calibration_written = experimental
        ? writeExperimentalMultispotError(
            calibration_pending, &input, "calibration", calibration_error, calibration_write_error)
        : writeSpotError(
            calibration_pending,
            input.task_id.empty() ? "unknown" : input.task_id,
            "calibration",
            calibration_error,
            calibration_write_error);
    const bool measurement_written = experimental
        ? writeExperimentalMultispotError(
            measurement_pending, &input, "measurement", measurement_error, measurement_write_error)
        : writeSpotError(
            measurement_pending,
            input.task_id.empty() ? "unknown" : input.task_id,
            "measurement",
            measurement_error,
            measurement_write_error);
    if (!calibration_written || !measurement_written) {
        write_error = calibration_written ? measurement_write_error : calibration_write_error;
        std::error_code ignored;
        std::filesystem::remove(calibration_pending, ignored);
        std::filesystem::remove(measurement_pending, ignored);
        return false;
    }
    return publishPair(calibration_pending, measurement_pending, result, write_error);
}

bool stageExperimentalMultispotSuccessPair(
    const RunResult& result,
    const InputPackage& input,
    const ImageAnalysis& calibration,
    const ImageAnalysis& measurement,
    const ProcessingConfig& config,
    ErrorInfo& write_error) {
    const auto calibration_pending = pendingPath(result.calibration_output);
    const auto measurement_pending = pendingPath(result.measurement_output);
    if (!writeExperimentalMultispotSuccess(
            calibration_pending, input, "calibration", calibration, config, write_error) ||
        !writeExperimentalMultispotSuccess(
            measurement_pending, input, "measurement", measurement, config, write_error)) {
        std::error_code ignored;
        std::filesystem::remove(calibration_pending, ignored);
        std::filesystem::remove(measurement_pending, ignored);
        return false;
    }
    return true;
}

bool stageSuccessPair(
    const RunResult& result,
    const InputPackage& input,
    const std::vector<Spot>& calibration_spots,
    const std::vector<Spot>& measurement_spots,
    const int expected_count,
    const std::vector<std::string>& calibration_warnings,
    const std::vector<std::string>& measurement_warnings,
    ErrorInfo& write_error) {
    const auto calibration_pending = pendingPath(result.calibration_output);
    const auto measurement_pending = pendingPath(result.measurement_output);
    if (!writeSpotSuccess(
            calibration_pending,
            input.schema_version,
            input.task_id,
            "calibration",
            calibration_spots,
            expected_count,
            calibration_warnings,
            write_error) ||
        !writeSpotSuccess(
            measurement_pending,
            input.schema_version,
            input.task_id,
            "measurement",
            measurement_spots,
            expected_count,
            measurement_warnings,
            write_error)) {
        std::error_code ignored;
        std::filesystem::remove(calibration_pending, ignored);
        std::filesystem::remove(measurement_pending, ignored);
        return false;
    }
    return true;
}

bool publishSuccessSet(const RunResult& result, ErrorInfo& error) {
    const auto log_pending = pendingPath(result.log_output);
    if (!publishPair(
            pendingPath(result.calibration_output),
            pendingPath(result.measurement_output),
            result,
            error)) {
        std::error_code ignored;
        std::filesystem::remove(log_pending, ignored);
        return false;
    }

    std::error_code rename_error;
    std::filesystem::rename(log_pending, result.log_output, rename_error);
    if (rename_error) {
        error = makeError("UNKNOWN_ERROR", "Could not publish the M2 run log.", false);
        error.string_details["output_file"] = result.log_output.filename().generic_string();
        std::error_code ignored;
        std::filesystem::remove(result.calibration_output, ignored);
        std::filesystem::remove(result.measurement_output, ignored);
        std::filesystem::remove(log_pending, ignored);
        return false;
    }
    return true;
}

RunResult unexpectedFailure(
    const RunOptions& options,
    const RecoveryContext& recovery,
    ErrorInfo unexpected_error) {
    const auto started_at = std::chrono::system_clock::now();
    RunResult result = makeRunResult(options);
    result.exit_code = 4;
    result.error = unexpected_error;

    // Do not invalidate managed outputs until every declared input has passed
    // the alias guard. This is deliberately conservative on partial preflight.
    if (!recovery.output_cleanup_is_safe) {
        return result;
    }

    OutputDirectoryLock output_lock;
    ErrorInfo lock_error;
    if (!output_lock.acquire(options.output_directory, lock_error)) {
        result.error = lock_error;
        return result;
    }

    ErrorInfo preparation_error;
    if (!prepareOutputTargets(result, preparation_error)) {
        result.error = preparation_error;
        return result;
    }

    const InputPackage input = recovery.input.value_or(InputPackage{});
    ErrorInfo pair_error;
    const bool pair_written = stageErrorPair(
        result, input, unexpected_error, unexpected_error, pair_error);
    const std::vector<std::filesystem::path> outputs{
        result.calibration_output, result.measurement_output};
    ErrorInfo log_error;
    const bool log_written = writeRunLog(
        result.log_output,
        input.task_id.empty() ? nullptr : &input,
        options,
        outputs,
        unexpected_error,
        {},
        started_at,
        log_error);
    if (!pair_written || !log_written) {
        result.error = pair_written ? log_error : pair_error;
    }
    return result;
}

}  // namespace

RunResult runImpl(const RunOptions& options, RecoveryContext& recovery) {
    const auto started_at = std::chrono::system_clock::now();
    RunResult result = makeRunResult(options);

    OutputDirectoryLock output_lock;
    ErrorInfo error;
    if (!output_lock.acquire(options.output_directory, error)) {
        result.exit_code = 4;
        result.error = error;
        return result;
    }

    if (!rejectInputOutputAliases(
            result,
            {{"input_package", options.input_package}},
            error)) {
        result.exit_code = 4;
        result.error = error;
        return result;
    }

    InputPackage input;
    if (!readInputPackage(options.input_package, input, error, options.recognition_mode)) {
        const ErrorInfo input_error = error;
        ErrorInfo alias_error;
        if (!rejectRawInputOutputAliases(result, options, input, alias_error)) {
            result.exit_code = 4;
            result.error = alias_error;
            return result;
        }
        if (!prepareOutputTargets(result, error)) {
            result.exit_code = 4;
            result.error = error;
            return result;
        }
        ErrorInfo write_error;
        const bool error_written = stageErrorPair(
            result, input, input_error, input_error, write_error);
        const std::vector<std::filesystem::path> outputs{
            result.calibration_output, result.measurement_output};
        ErrorInfo log_error;
        const bool log_written = writeRunLog(
            result.log_output,
            input.task_id.empty() ? nullptr : &input,
            options,
            outputs,
            input_error,
            {},
            started_at,
            log_error);
        result.exit_code = error_written && log_written ? 2 : 4;
        result.error = error_written && log_written ? input_error : (error_written ? log_error : write_error);
        return result;
    }

    recovery.input = input;
    if (!rejectRawInputOutputAliases(result, options, input, error)) {
        result.exit_code = 4;
        result.error = error;
        return result;
    }

    const auto finish_pair_failure = [
        &result,
        &input,
        &options,
        started_at](
            const int exit_code,
            const ErrorInfo& calibration_error,
            const ErrorInfo& measurement_error,
            const ErrorInfo& primary_error) -> RunResult {
        ErrorInfo preparation_error;
        if (!prepareOutputTargets(result, preparation_error)) {
            result.exit_code = 4;
            result.error = preparation_error;
            return result;
        }
        ErrorInfo write_error;
        const bool pair_written = stageErrorPair(
            result, input, calibration_error, measurement_error, write_error);
        const std::vector<std::filesystem::path> outputs{
            result.calibration_output, result.measurement_output};
        ErrorInfo log_error;
        const bool log_written = writeRunLog(
            result.log_output,
            &input,
            options,
            outputs,
            primary_error,
            {},
            started_at,
            log_error);
        result.exit_code = pair_written && log_written ? exit_code : 4;
        if (!pair_written || !log_written) {
            result.error = pair_written ? log_error : write_error;
        } else {
            result.error = primary_error;
        }
        return result;
    };

    // Root discovery probes config_path, so reject unsafe raw paths before any filesystem access.
    if (!validateProjectRelativePath(
            input.config_path, "data.config_path", error) ||
        !validateProjectRelativePath(
            input.calibration_image, "data.calibration_image", error) ||
        !validateProjectRelativePath(
            input.measurement_image, "data.measurement_image", error)) {
        return finish_pair_failure(2, error, error, error);
    }

    const auto project_root = discoverProjectRoot(options, input);
    const auto declared_path = [&project_root](const std::filesystem::path& path) {
        return project_root / path;
    };
    if (!rejectInputOutputAliases(
            result,
            {
                {"input_package", options.input_package},
                {"data.config_path", declared_path(input.config_path)},
                {"data.calibration_image", declared_path(input.calibration_image)},
                {"data.measurement_image", declared_path(input.measurement_image)},
            },
            error)) {
        result.exit_code = 4;
        result.error = error;
        return result;
    }
    recovery.output_cleanup_is_safe = true;

    const auto config_path = resolveInsideProject(project_root, input.config_path, "data.config_path", error);
    if (!config_path.has_value()) {
        return finish_pair_failure(2, error, error, error);
    }

    const auto calibration_path = resolveInsideProject(
        project_root, input.calibration_image, "data.calibration_image", error);
    if (!calibration_path.has_value()) {
        return finish_pair_failure(2, error, error, error);
    }
    const auto measurement_path = resolveInsideProject(
        project_root, input.measurement_image, "data.measurement_image", error);
    if (!measurement_path.has_value()) {
        return finish_pair_failure(2, error, error, error);
    }

    if (!rejectInputOutputAliases(
            result,
            {
                {"input_package", options.input_package},
                {"data.config_path", *config_path},
                {"data.calibration_image", *calibration_path},
                {"data.measurement_image", *measurement_path},
            },
            error)) {
        result.exit_code = 4;
        result.error = error;
        return result;
    }

    ProcessingConfig config;
    config.recognition_mode = options.recognition_mode;
    config.multispot_16bit_white_level = options.experimental_16bit_white_level;
    if (!readProcessingConfig(*config_path, config, error)) {
        error.string_details["config_path"] = input.config_path.generic_string();
        return finish_pair_failure(2, error, error, error);
    }
    if (options.recognition_mode == RecognitionMode::HartmannMultispotExperimental &&
        input.data_source == "unknown" && config.data_source != "unknown") {
        input.data_source = config.data_source;
    }
    ImageProcessor processor;
    if (!prepareOutputTargets(result, error)) {
        result.exit_code = 4;
        result.error = error;
        return result;
    }
    ImageAnalysis calibration = processor.processFile(*calibration_path, config);
    ImageAnalysis measurement = processor.processFile(*measurement_path, config);
    replaceImagePathDetail(calibration.error, input.calibration_image);
    replaceImagePathDetail(measurement.error, input.measurement_image);

    const auto save_processed_artifacts = [
        &options,
        &input,
        &result,
        &calibration,
        &measurement](
            const std::vector<Spot>& calibration_spots,
            const std::vector<Spot>& measurement_spots,
            ErrorInfo& artifact_error) {
        if (!options.save_intermediate) {
            return true;
        }
        const auto intermediate_directory = result.calibration_output.parent_path() / "intermediate";
        if (saveArtifacts(
                intermediate_directory,
                input.task_id,
                "calibration",
                calibration,
                calibration_spots,
                artifact_error) &&
            saveArtifacts(
                intermediate_directory,
                input.task_id,
                "measurement",
                measurement,
                measurement_spots,
                artifact_error)) {
            return true;
        }
        ErrorInfo ignored;
        removeIntermediateOutputs(result, ignored);
        return false;
    };

    const auto finish_processed_failure = [
        &finish_pair_failure,
        &save_processed_artifacts](
            const int exit_code,
            const ErrorInfo& calibration_error,
            const ErrorInfo& measurement_error,
            const ErrorInfo& primary_error,
            const std::vector<Spot>& calibration_spots,
            const std::vector<Spot>& measurement_spots) {
        RunResult failure = finish_pair_failure(
            exit_code, calibration_error, measurement_error, primary_error);
        if (failure.exit_code != exit_code) {
            return failure;
        }
        ErrorInfo artifact_error;
        if (!save_processed_artifacts(
                calibration_spots, measurement_spots, artifact_error)) {
            return finish_pair_failure(
                4, artifact_error, artifact_error, artifact_error);
        }
        return failure;
    };

    const std::vector<Spot> no_spots;

    if (!calibration.original.empty() && !measurement.original.empty() &&
        calibration.original.size() != measurement.original.size()) {
        ErrorInfo dimension_error = makeError(
            "COORDINATE_SYSTEM_INVALID",
            "Calibration and measurement images must have the same pixel dimensions.",
            true);
        dimension_error.number_details["calibration_width"] = calibration.original.cols;
        dimension_error.number_details["calibration_height"] = calibration.original.rows;
        dimension_error.number_details["measurement_width"] = measurement.original.cols;
        dimension_error.number_details["measurement_height"] = measurement.original.rows;
        return finish_processed_failure(
            3,
            dimension_error,
            dimension_error,
            dimension_error,
            no_spots,
            no_spots);
    }

    const auto reject_declared_dimensions = [
        &config](const ImageAnalysis& analysis, const std::string& image_type) {
        ErrorInfo dimension_error;
        if (analysis.original.empty()) {
            return dimension_error;
        }
        const bool width_mismatch = config.declared_image_width.has_value() &&
            analysis.original.cols != *config.declared_image_width;
        const bool height_mismatch = config.declared_image_height.has_value() &&
            analysis.original.rows != *config.declared_image_height;
        if (!width_mismatch && !height_mismatch) {
            return dimension_error;
        }
        dimension_error = makeError(
            "CONFIG_INVALID",
            "Decoded image dimensions do not match the unified camera configuration.",
            true);
        dimension_error.string_details["image_type"] = image_type;
        dimension_error.number_details["actual_width"] = analysis.original.cols;
        dimension_error.number_details["actual_height"] = analysis.original.rows;
        if (config.declared_image_width.has_value()) {
            dimension_error.number_details["declared_width"] = *config.declared_image_width;
        }
        if (config.declared_image_height.has_value()) {
            dimension_error.number_details["declared_height"] = *config.declared_image_height;
        }
        return dimension_error;
    };

    ErrorInfo calibration_dimension_error;
    ErrorInfo measurement_dimension_error;
    if (!calibration.original.empty() && !measurement.original.empty()) {
        calibration_dimension_error =
            reject_declared_dimensions(calibration, "calibration");
        measurement_dimension_error =
            reject_declared_dimensions(measurement, "measurement");
    }
    if (!calibration_dimension_error.empty() || !measurement_dimension_error.empty()) {
        const ErrorInfo primary_error = calibration_dimension_error.empty()
            ? measurement_dimension_error
            : calibration_dimension_error;
        const ErrorInfo calibration_error = calibration_dimension_error.empty()
            ? primary_error
            : calibration_dimension_error;
        const ErrorInfo measurement_error = measurement_dimension_error.empty()
            ? primary_error
            : measurement_dimension_error;
        return finish_processed_failure(
            2,
            calibration_error,
            measurement_error,
            primary_error,
            no_spots,
            no_spots);
    }

    if (!calibration.ok() || !measurement.ok()) {
        const ErrorInfo primary_error = calibration.ok() ? measurement.error : calibration.error;
        ErrorInfo calibration_error = calibration.error;
        ErrorInfo measurement_error = measurement.error;
        if (calibration_error.empty()) {
            calibration_error = makeError(
                "COORDINATE_SYSTEM_INVALID",
                "Calibration output cannot be paired because measurement processing failed.",
                true);
        }
        if (measurement_error.empty()) {
            measurement_error = makeError(
                "COORDINATE_SYSTEM_INVALID",
                "Measurement output cannot be paired because calibration processing failed.",
                true);
        }
        return finish_processed_failure(
            3,
            calibration_error,
            measurement_error,
            primary_error,
            no_spots,
            no_spots);
    }

    if (config.recognition_mode == RecognitionMode::HartmannMultispotExperimental) {
        const std::vector<Spot> calibration_display = makeExperimentalDisplaySpots(calibration);
        const std::vector<Spot> measurement_display = makeExperimentalDisplaySpots(measurement);
        if (options.save_intermediate) {
            ErrorInfo artifact_error;
            if (!save_processed_artifacts(
                    calibration_display, measurement_display, artifact_error)) {
                return finish_pair_failure(4, artifact_error, artifact_error, artifact_error);
            }
        }

        std::vector<std::string> run_warnings;
        for (const auto& warning : calibration.diagnostics.warnings) {
            run_warnings.push_back("CALIBRATION_" + warning);
        }
        for (const auto& warning : measurement.diagnostics.warnings) {
            run_warnings.push_back("MEASUREMENT_" + warning);
        }

        ErrorInfo write_error;
        if (!stageExperimentalMultispotSuccessPair(
                result, input, calibration, measurement, config, write_error)) {
            result.exit_code = 4;
            result.error = write_error;
            return result;
        }
        const std::vector<std::filesystem::path> outputs{
            result.calibration_output, result.measurement_output};
        if (!writeRunLog(
                pendingPath(result.log_output),
                &input,
                options,
                outputs,
                {},
                run_warnings,
                started_at,
                write_error)) {
            std::error_code ignored;
            std::filesystem::remove(pendingPath(result.calibration_output), ignored);
            std::filesystem::remove(pendingPath(result.measurement_output), ignored);
            result.exit_code = 4;
            result.error = write_error;
            return result;
        }
        if (!publishSuccessSet(result, write_error)) {
            result.exit_code = 4;
            result.error = write_error;
            return result;
        }
        result.exit_code = 0;
        return result;
    }

    SpotMatcher matcher;
    std::vector<Spot> calibration_spots;
    if (!matcher.assignCalibrationRoles(calibration.observations, calibration_spots, error)) {
        return finish_processed_failure(
            3, error, error, error, calibration_spots, no_spots);
    }

    std::vector<Spot> measurement_spots;
    MatchDiagnostics diagnostics;
    if (!matcher.matchMeasurement(
            calibration_spots,
            measurement.observations,
            config,
            measurement_spots,
            diagnostics,
            error)) {
        return finish_processed_failure(
            3, error, error, error, calibration_spots, measurement_spots);
    }

    std::vector<std::string> calibration_warnings = calibration.diagnostics.warnings;
    std::vector<std::string> measurement_warnings = measurement.diagnostics.warnings;
    std::vector<std::string> run_warnings;
    for (const auto& warning : calibration.diagnostics.warnings) {
        run_warnings.push_back("CALIBRATION_" + warning);
    }
    for (const auto& warning : measurement.diagnostics.warnings) {
        run_warnings.push_back("MEASUREMENT_" + warning);
    }
    if (std::abs(diagnostics.rotation_degrees) > 20.0) {
        calibration_warnings.push_back("MATCH_ROTATION_HIGH");
        measurement_warnings.push_back("MATCH_ROTATION_HIGH");
        run_warnings.push_back("MATCH_ROTATION_HIGH");
    }
    if (options.save_intermediate) {
        ErrorInfo artifact_error;
        if (!save_processed_artifacts(
                calibration_spots, measurement_spots, artifact_error)) {
            return finish_pair_failure(4, artifact_error, artifact_error, artifact_error);
        }
    }

    ErrorInfo write_error;
    if (!stageSuccessPair(
            result,
            input,
            calibration_spots,
            measurement_spots,
            config.expected_spot_count,
            calibration_warnings,
            measurement_warnings,
            write_error)) {
        result.exit_code = 4;
        result.error = write_error;
        return result;
    }

    const std::vector<std::filesystem::path> outputs{
        result.calibration_output, result.measurement_output};
    if (!writeRunLog(
            pendingPath(result.log_output),
            &input,
            options,
            outputs,
            {},
            run_warnings,
            started_at,
            write_error)) {
        std::error_code ignored;
        std::filesystem::remove(pendingPath(result.calibration_output), ignored);
        std::filesystem::remove(pendingPath(result.measurement_output), ignored);
        result.exit_code = 4;
        result.error = write_error;
        return result;
    }

    if (!publishSuccessSet(result, write_error)) {
        result.exit_code = 4;
        result.error = write_error;
        return result;
    }

    result.exit_code = 0;
    return result;
}

RunResult ImageRecognitionModule::run(const RunOptions& options) const {
    RecoveryContext recovery;
    try {
        return runImpl(options, recovery);
    } catch (const cv::Exception& exception) {
        ErrorInfo error = makeError(
            "UNKNOWN_ERROR", "OpenCV raised an unexpected M2 processing error.", false);
        error.string_details["reason"] = exception.what();
        return unexpectedFailure(options, recovery, std::move(error));
    } catch (const std::exception& exception) {
        ErrorInfo error = makeError(
            "UNKNOWN_ERROR", "M2 raised an unexpected processing error.", false);
        error.string_details["reason"] = exception.what();
        return unexpectedFailure(options, recovery, std::move(error));
    }
}

}  // namespace focimeter::m2
