#include "focimeter/m2/json_io.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <initializer_list>
#include <limits>
#include <set>
#include <sstream>
#include <string_view>

#include <nlohmann/json.hpp>

namespace focimeter::m2 {
namespace {

using Json = nlohmann::json;

ErrorInfo makeError(std::string code, std::string message, const bool recoverable) {
    ErrorInfo error;
    error.code = std::move(code);
    error.message = std::move(message);
    error.recoverable = recoverable;
    return error;
}

bool readJson(
    const std::filesystem::path& path,
    Json& document,
    ErrorInfo& error,
    const std::string& missing_code,
    const std::string& invalid_code) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        error = makeError(missing_code, "Could not open JSON input file.", true);
        error.string_details["file"] = path.filename().generic_string();
        return false;
    }
    try {
        document = Json::parse(stream, nullptr, true, false);
    } catch (const Json::exception& exception) {
        error = makeError(invalid_code, "JSON input cannot be parsed.", true);
        error.string_details["file"] = path.filename().generic_string();
        error.string_details["reason"] = exception.what();
        return false;
    }
    if (!document.is_object()) {
        error = makeError(invalid_code, "JSON root must be an object.", true);
        error.string_details["file"] = path.filename().generic_string();
        return false;
    }
    return true;
}

bool requireString(
    const Json& parent,
    const char* key,
    std::string& value,
    ErrorInfo& error,
    const std::string_view context,
    const std::string& error_code) {
    const auto iterator = parent.find(key);
    if (iterator == parent.end() || !iterator->is_string() || iterator->get_ref<const std::string&>().empty()) {
        error = makeError(error_code, "Required JSON string field is missing, empty, or invalid.", true);
        error.string_details["field"] = std::string(context) + "." + key;
        return false;
    }
    value = iterator->get<std::string>();
    return true;
}

bool requireNumber(
    const Json& parent,
    const char* key,
    double& value,
    ErrorInfo& error,
    const std::string_view context) {
    const auto iterator = parent.find(key);
    if (iterator == parent.end() || !iterator->is_number()) {
        error = makeError("CONFIG_INVALID", "Required configuration number is missing or invalid.", true);
        error.string_details["field"] = std::string(context) + "." + key;
        return false;
    }
    value = iterator->get<double>();
    if (!std::isfinite(value)) {
        error = makeError("CONFIG_INVALID", "Configuration numbers must be finite.", true);
        error.string_details["field"] = std::string(context) + "." + key;
        return false;
    }
    return true;
}

bool requireInteger(
    const Json& parent,
    const char* key,
    int& value,
    ErrorInfo& error,
    const std::string_view context) {
    const auto iterator = parent.find(key);
    if (iterator == parent.end() || (!iterator->is_number_integer() && !iterator->is_number_unsigned())) {
        error = makeError("CONFIG_INVALID", "Required configuration integer is missing or invalid.", true);
        error.string_details["field"] = std::string(context) + "." + key;
        return false;
    }
    try {
        value = iterator->get<int>();
    } catch (const Json::exception&) {
        error = makeError("CONFIG_INVALID", "Configuration integer is outside the supported range.", true);
        error.string_details["field"] = std::string(context) + "." + key;
        return false;
    }
    return true;
}

bool requireExactKeys(
    const Json& object,
    const std::initializer_list<std::string_view> expected_keys,
    ErrorInfo& error,
    const std::string_view context) {
    if (!object.is_object() || object.size() != expected_keys.size()) {
        error = makeError("CONFIG_INVALID", "Configuration object fields do not match the unified schema.", true);
        error.string_details["field"] = std::string(context);
        return false;
    }
    for (const auto key : expected_keys) {
        if (!object.contains(std::string(key))) {
            error = makeError("CONFIG_INVALID", "Required unified configuration field is missing.", true);
            error.string_details["field"] = std::string(context) + "." + std::string(key);
            return false;
        }
    }
    return true;
}

bool requirePositiveOrUnknown(
    const Json& object,
    const char* key,
    const bool integer_only,
    ErrorInfo& error,
    const std::string_view context) {
    const auto iterator = object.find(key);
    const std::string field = std::string(context) + "." + key;
    if (iterator == object.end()) {
        error = makeError("CONFIG_INVALID", "Required unified configuration field is missing.", true);
        error.string_details["field"] = field;
        return false;
    }
    if (iterator->is_null() || (iterator->is_string() && iterator->get_ref<const std::string&>() == "TODO_CONFIRM")) {
        return true;
    }
    if (integer_only) {
        if ((!iterator->is_number_integer() && !iterator->is_number_unsigned()) ||
            !std::isfinite(iterator->get<double>()) || iterator->get<double>() < 1.0) {
            error = makeError("CONFIG_INVALID", "Configuration value must be a positive integer or an allowed unknown marker.", true);
            error.string_details["field"] = field;
            return false;
        }
        return true;
    }
    if (!iterator->is_number()) {
        error = makeError("CONFIG_INVALID", "Configuration value must be a positive number or an allowed unknown marker.", true);
        error.string_details["field"] = field;
        return false;
    }
    const double value = iterator->get<double>();
    if (!std::isfinite(value) || value <= 0.0) {
        error = makeError("CONFIG_INVALID", "Configuration value must be finite and positive.", true);
        error.string_details["field"] = field;
        return false;
    }
    return true;
}

bool readOptionalPositiveInteger(
    const Json& object,
    const char* key,
    std::optional<int>& destination,
    ErrorInfo& error,
    const std::string_view context) {
    const auto iterator = object.find(key);
    const std::string field = std::string(context) + "." + key;
    if (iterator == object.end()) {
        error = makeError("CONFIG_INVALID", "Required unified configuration field is missing.", true);
        error.string_details["field"] = field;
        return false;
    }
    if (iterator->is_null() ||
        (iterator->is_string() && iterator->get_ref<const std::string&>() == "TODO_CONFIRM")) {
        destination.reset();
        return true;
    }
    if ((!iterator->is_number_integer() && !iterator->is_number_unsigned()) ||
        iterator->get<double>() < 1.0 ||
        iterator->get<double>() > static_cast<double>(std::numeric_limits<int>::max())) {
        error = makeError(
            "CONFIG_INVALID",
            "Configuration value must fit a positive integer or use an allowed unknown marker.",
            true);
        error.string_details["field"] = field;
        return false;
    }
    destination = iterator->get<int>();
    return true;
}

Json errorToJson(const ErrorInfo& error) {
    Json details = Json::object();
    for (const auto& [key, value] : error.string_details) {
        details[key] = value;
    }
    for (const auto& [key, value] : error.number_details) {
        details[key] = value;
    }
    return {
        {"code", error.code},
        {"message", error.message},
        {"module", "m2_image_recognition"},
        {"recoverable", error.recoverable},
        {"details", std::move(details)},
    };
}

bool writeJson(const std::filesystem::path& path, const Json& document, ErrorInfo& error) {
    std::error_code directory_error;
    std::filesystem::create_directories(path.parent_path(), directory_error);
    if (directory_error) {
        error = makeError("UNKNOWN_ERROR", "Could not create output directory.", false);
        error.string_details["output_file"] = path.filename().generic_string();
        return false;
    }

    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) {
        error = makeError("UNKNOWN_ERROR", "Could not write JSON output file.", false);
        error.string_details["output_file"] = path.filename().generic_string();
        return false;
    }
    stream << document.dump(2) << '\n';
    stream.flush();
    if (!stream.good()) {
        error = makeError("UNKNOWN_ERROR", "Writing JSON output file failed.", false);
        error.string_details["output_file"] = path.filename().generic_string();
        return false;
    }
    stream.close();
    if (stream.fail()) {
        error = makeError("UNKNOWN_ERROR", "Closing JSON output file failed.", false);
        error.string_details["output_file"] = path.filename().generic_string();
        return false;
    }
    return true;
}

std::string timestamp(const std::chrono::system_clock::time_point time_point) {
    const std::time_t time = std::chrono::system_clock::to_time_t(time_point);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &time);
#else
    localtime_r(&time, &local);
#endif
    std::ostringstream output;
    output << std::put_time(&local, "%Y-%m-%d %H:%M:%S");
    return output.str();
}

}  // namespace

bool readInputPackage(
    const std::filesystem::path& path,
    InputPackage& input,
    ErrorInfo& error) {
    input = {};
    error = {};
    Json root;
    if (!readJson(path, root, error, "UNKNOWN_ERROR", "UNKNOWN_ERROR")) {
        return false;
    }

    std::string module;
    std::string status;
    if (!requireString(root, "schema_version", input.schema_version, error, "root", "UNKNOWN_ERROR") ||
        !requireString(root, "task_id", input.task_id, error, "root", "UNKNOWN_ERROR") ||
        !requireString(root, "module", module, error, "root", "UNKNOWN_ERROR") ||
        !requireString(root, "status", status, error, "root", "UNKNOWN_ERROR")) {
        return false;
    }
    if (input.schema_version != "1.0" || module != "m1_input_config" || status != "ok") {
        error = makeError("UNKNOWN_ERROR", "M1 input package is not a usable v1 success result.", true);
        error.string_details["schema_version"] = input.schema_version;
        error.string_details["module"] = module;
        error.string_details["status"] = status;
        return false;
    }

    const auto data_iterator = root.find("data");
    if (data_iterator == root.end() || !data_iterator->is_object()) {
        error = makeError("UNKNOWN_ERROR", "M1 input package does not contain a data object.", true);
        return false;
    }
    const Json& data = *data_iterator;
    std::string calibration;
    std::string measurement;
    std::string config;
    if (!requireString(data, "calibration_image", calibration, error, "data", "UNKNOWN_ERROR") ||
        !requireString(data, "measurement_image", measurement, error, "data", "UNKNOWN_ERROR") ||
        !requireString(data, "config_path", config, error, "data", "UNKNOWN_ERROR") ||
        !requireString(data, "run_mode", input.run_mode, error, "data", "UNKNOWN_ERROR")) {
        return false;
    }
    input.calibration_image = std::filesystem::u8path(calibration);
    input.measurement_image = std::filesystem::u8path(measurement);
    input.config_path = std::filesystem::u8path(config);

    const auto quality = root.find("quality");
    if (quality != root.end()) {
        if (!quality->is_object()) {
            error = makeError("UNKNOWN_ERROR", "M1 quality must be an object when present.", true);
            return false;
        }
        const auto usable = quality->find("is_usable");
        if (usable != quality->end()) {
            if (!usable->is_boolean()) {
                error = makeError("UNKNOWN_ERROR", "M1 quality.is_usable must be a boolean.", true);
                return false;
            }
            if (!usable->get<bool>()) {
                error = makeError("UNKNOWN_ERROR", "M1 marked the input package as unusable.", true);
                return false;
            }
        }
    }
    if (input.run_mode != "local_image") {
        error = makeError("UNKNOWN_ERROR", "M2 first-stage execution only supports run_mode 'local_image'.", true);
        error.string_details["run_mode"] = input.run_mode;
        return false;
    }
    return true;
}

bool readProcessingConfig(
    const std::filesystem::path& path,
    ProcessingConfig& config,
    ErrorInfo& error) {
    config = {};
    error = {};
    Json root;
    if (!readJson(path, root, error, "CONFIG_NOT_FOUND", "CONFIG_INVALID")) {
        return false;
    }

    std::string schema_version;
    if (!requireExactKeys(
            root,
            {"schema_version", "config_name", "camera", "optical", "image_processing", "recognition", "calculation", "path_policy"},
            error,
            "root") ||
        !requireString(root, "schema_version", schema_version, error, "root", "CONFIG_INVALID") ||
        schema_version != "1.0") {
        if (error.empty()) {
            error = makeError("CONFIG_INVALID", "Configuration schema_version must be 1.0.", true);
        }
        return false;
    }
    std::string config_name;
    if (!requireString(root, "config_name", config_name, error, "root", "CONFIG_INVALID")) {
        return false;
    }
    const auto camera_iterator = root.find("camera");
    const auto optical_iterator = root.find("optical");
    const auto image_iterator = root.find("image_processing");
    const auto recognition_iterator = root.find("recognition");
    const auto calculation_iterator = root.find("calculation");
    const auto path_policy_iterator = root.find("path_policy");
    if (camera_iterator == root.end() || optical_iterator == root.end() ||
        image_iterator == root.end() || recognition_iterator == root.end() ||
        calculation_iterator == root.end() || path_policy_iterator == root.end() ||
        !camera_iterator->is_object() || !optical_iterator->is_object() ||
        !image_iterator->is_object() || !recognition_iterator->is_object() ||
        !calculation_iterator->is_object() || !path_policy_iterator->is_object()) {
        error = makeError("CONFIG_INVALID", "Unified configuration sections must be JSON objects.", true);
        return false;
    }
    const Json& camera = *camera_iterator;
    const Json& optical = *optical_iterator;
    const Json& image = *image_iterator;
    const Json& recognition = *recognition_iterator;
    const Json& calculation = *calculation_iterator;
    const Json& path_policy = *path_policy_iterator;

    if (!requireExactKeys(camera, {"pixel_size_um", "image_width", "image_height"}, error, "camera") ||
        !requireExactKeys(optical, {"distance_m", "hartmann_spacing_mm"}, error, "optical") ||
        !requireExactKeys(
            image,
            {"roi_width_ratio", "roi_height_ratio", "median_kernel", "tophat_kernel", "otsu_a", "otsu_b", "max_depth"},
            error,
            "image_processing") ||
        !requireExactKeys(recognition, {"expected_spot_count", "min_confidence"}, error, "recognition") ||
        !requireExactKeys(calculation, {"pixel_threshold", "angle_unit", "diopter_unit"}, error, "calculation") ||
        !requireExactKeys(path_policy, {"path_type", "allow_absolute_path"}, error, "path_policy") ||
        !requirePositiveOrUnknown(camera, "pixel_size_um", false, error, "camera") ||
        !readOptionalPositiveInteger(
            camera, "image_width", config.declared_image_width, error, "camera") ||
        !readOptionalPositiveInteger(
            camera, "image_height", config.declared_image_height, error, "camera") ||
        !requirePositiveOrUnknown(optical, "distance_m", false, error, "optical") ||
        !requirePositiveOrUnknown(optical, "hartmann_spacing_mm", false, error, "optical")) {
        return false;
    }

    if (!requireNumber(image, "roi_width_ratio", config.roi_width_ratio, error, "image_processing") ||
        !requireNumber(image, "roi_height_ratio", config.roi_height_ratio, error, "image_processing") ||
        !requireInteger(image, "median_kernel", config.median_kernel, error, "image_processing") ||
        !requireInteger(image, "tophat_kernel", config.tophat_kernel, error, "image_processing") ||
        !requireNumber(image, "otsu_a", config.otsu_a, error, "image_processing") ||
        !requireNumber(image, "otsu_b", config.otsu_b, error, "image_processing") ||
        !requireInteger(image, "max_depth", config.max_depth, error, "image_processing") ||
        !requireInteger(recognition, "expected_spot_count", config.expected_spot_count, error, "recognition") ||
        !requireNumber(recognition, "min_confidence", config.min_confidence, error, "recognition")) {
        return false;
    }

    if (config.roi_width_ratio <= 0.0 || config.roi_width_ratio > 1.0 ||
        config.roi_height_ratio <= 0.0 || config.roi_height_ratio > 1.0 ||
        config.median_kernel < 1 || config.median_kernel % 2 == 0 ||
        config.tophat_kernel < 1 || config.tophat_kernel > 4096 ||
        config.otsu_a <= 0.0 || config.otsu_a >= config.otsu_b ||
        config.otsu_b > 1.0 || config.max_depth < 0 || config.max_depth > 8 ||
        config.expected_spot_count != 5 ||
        config.min_confidence < 0.0 || config.min_confidence > 1.0) {
        error = makeError("CONFIG_INVALID", "Processing configuration values are outside M2's supported range.", true);
        return false;
    }

    double pixel_threshold = 0.0;
    std::string angle_unit;
    std::string diopter_unit;
    std::string path_type;
    if (!requireNumber(calculation, "pixel_threshold", pixel_threshold, error, "calculation") ||
        !requireString(calculation, "angle_unit", angle_unit, error, "calculation", "CONFIG_INVALID") ||
        !requireString(calculation, "diopter_unit", diopter_unit, error, "calculation", "CONFIG_INVALID") ||
        !requireString(path_policy, "path_type", path_type, error, "path_policy", "CONFIG_INVALID")) {
        return false;
    }
    const auto allow_absolute = path_policy.find("allow_absolute_path");
    if (pixel_threshold < 0.0 || angle_unit != "degree" || diopter_unit != "D" ||
        path_type != "relative_to_project_root" ||
        allow_absolute == path_policy.end() || !allow_absolute->is_boolean() || allow_absolute->get<bool>()) {
        error = makeError("CONFIG_INVALID", "Unified calculation units or path policy are invalid.", true);
        return false;
    }
    return true;
}

bool writeSpotSuccess(
    const std::filesystem::path& path,
    const std::string& schema_version,
    const std::string& task_id,
    const std::string& image_type,
    const std::vector<Spot>& spots,
    const int expected_count,
    const std::vector<std::string>& warnings,
    ErrorInfo& error) {
    error = {};
    const std::array<const char*, 5> expected_roles{
        "center", "y_positive", "left_or_negative", "other", "x_positive"};
    if (schema_version != "1.0" || task_id.empty() ||
        (image_type != "calibration" && image_type != "measurement") ||
        expected_count != 5 || spots.size() != 5) {
        error = makeError(
            "COORDINATE_SYSTEM_INVALID",
            "M2 refused to serialize an invalid success result.",
            false);
        return false;
    }
    std::set<int> ids;
    for (const auto& spot : spots) {
        if (spot.spot_id < 0 || spot.spot_id >= expected_count ||
            spot.role != expected_roles[static_cast<std::size_t>(spot.spot_id)] ||
            !std::isfinite(spot.center.x) || !std::isfinite(spot.center.y) ||
            !std::isfinite(spot.confidence) ||
            spot.confidence < 0.0 || spot.confidence > 1.0 ||
            !ids.insert(spot.spot_id).second) {
            error = makeError(
                "COORDINATE_SYSTEM_INVALID",
                "M2 refused to serialize inconsistent spot IDs, roles, coordinates, or confidence.",
                false);
            return false;
        }
    }
    double minimum_confidence = 1.0;
    Json spot_values = Json::array();
    for (const auto& spot : spots) {
        minimum_confidence = std::min(minimum_confidence, spot.confidence);
        spot_values.push_back({
            {"spot_id", spot.spot_id},
            {"role", spot.role},
            {"x", spot.center.x},
            {"y", spot.center.y},
            {"confidence", spot.confidence},
        });
    }
    const Json document = {
        {"schema_version", schema_version},
        {"task_id", task_id},
        {"module", "m2_image_recognition"},
        {"status", "ok"},
        {"image_type", image_type},
        {"coordinate_type", "image_pixel"},
        {"spots", std::move(spot_values)},
        {"quality", {
            {"expected_count", expected_count},
            {"detected_count", spots.size()},
            {"min_confidence", minimum_confidence},
            {"is_usable", true},
            {"warnings", warnings},
        }},
        {"error", nullptr},
    };
    return writeJson(path, document, error);
}

bool writeSpotError(
    const std::filesystem::path& path,
    const std::string& task_id,
    const std::string& image_type,
    const ErrorInfo& module_error,
    ErrorInfo& write_error) {
    write_error = {};
    const Json document = {
        {"schema_version", "1.0"},
        {"task_id", task_id.empty() ? "unknown" : task_id},
        {"module", "m2_image_recognition"},
        {"status", "error"},
        {"image_type", image_type},
        {"error", errorToJson(module_error)},
    };
    return writeJson(path, document, write_error);
}

bool writeImageDiagnostics(
    const std::filesystem::path& path,
    const std::string& task_id,
    const std::string& image_type,
    const ImageAnalysis& analysis,
    ErrorInfo& write_error) {
    write_error = {};
    if (task_id.empty() ||
        (image_type != "calibration" && image_type != "measurement")) {
        write_error = makeError(
            "UNKNOWN_ERROR",
            "M2 refused to serialize diagnostics for an unknown image type.",
            false);
        return false;
    }

    const auto& diagnostics = analysis.diagnostics;
    const Json document = {
        {"schema_version", "1.0"},
        {"task_id", task_id},
        {"module", "m2_image_recognition"},
        {"image_type", image_type},
        {"status", analysis.ok() ? "ok" : "error"},
        {"validation_status", "software_verified"},
        {"metrology_validated", false},
        {"image", {
            {"width", diagnostics.image_width},
            {"height", diagnostics.image_height},
            {"channels", diagnostics.channels},
            {"source_depth_bits", diagnostics.source_depth_bits},
        }},
        {"intensity", {
            {"domain", "normalized_8bit_roi"},
            {"mean", diagnostics.mean_intensity},
            {"standard_deviation", diagnostics.intensity_stddev},
            {"minimum", diagnostics.minimum_intensity},
            {"maximum", diagnostics.maximum_intensity},
            {"dark_pixel_ratio", diagnostics.dark_pixel_ratio},
            {"bright_pixel_ratio", diagnostics.bright_pixel_ratio},
        }},
        {"detection", {{"candidate_count", diagnostics.candidate_count}}},
        {"warnings", diagnostics.warnings},
        {"error", analysis.error.empty() ? Json(nullptr) : errorToJson(analysis.error)},
    };
    return writeJson(path, document, write_error);
}

bool writeRunLog(
    const std::filesystem::path& path,
    const InputPackage* input,
    const RunOptions& options,
    const std::vector<std::filesystem::path>& outputs,
    const ErrorInfo& run_error,
    const std::vector<std::string>& warnings,
    const std::chrono::system_clock::time_point started_at,
    ErrorInfo& write_error) {
    write_error = {};
    const auto ended_at = std::chrono::system_clock::now();
    Json output_files = Json::array();
    for (const auto& output : outputs) {
        output_files.push_back(output.filename().generic_string());
    }
    Json document = {
        {"schema_version", "1.0"},
        {"task_id", input == nullptr ? "unknown" : input->task_id},
        {"module", "m2_image_recognition"},
        {"validation_status", "software_verified"},
        {"metrology_validated", false},
        {"start_time", timestamp(started_at)},
        {"end_time", timestamp(ended_at)},
        {"duration_ms", std::chrono::duration_cast<std::chrono::milliseconds>(ended_at - started_at).count()},
        {"status", run_error.empty() ? "ok" : "error"},
        {"input_files", Json::array({options.input_package.filename().generic_string()})},
        {"output_files", std::move(output_files)},
        {"parameters", {{"save_intermediate", options.save_intermediate}}},
        {"warnings", warnings},
        {"error", run_error.empty() ? Json(nullptr) : errorToJson(run_error)},
    };
    return writeJson(path, document, write_error);
}

}  // namespace focimeter::m2
