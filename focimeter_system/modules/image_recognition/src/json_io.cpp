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

bool requireAllowedKeys(
    const Json& object,
    const std::initializer_list<std::string_view> required_keys,
    const std::initializer_list<std::string_view> allowed_keys,
    ErrorInfo& error,
    const std::string_view context) {
    if (!object.is_object()) {
        error = makeError("CONFIG_INVALID", "Configuration section must be a JSON object.", true);
        error.string_details["field"] = std::string(context);
        return false;
    }
    std::set<std::string> allowed;
    for (const auto key : allowed_keys) {
        allowed.emplace(key);
    }
    for (const auto& item : object.items()) {
        if (allowed.find(item.key()) == allowed.end()) {
            error = makeError("CONFIG_INVALID", "Configuration object contains an undeclared field.", true);
            error.string_details["field"] = std::string(context) + "." + item.key();
            return false;
        }
    }
    for (const auto key : required_keys) {
        if (!object.contains(std::string(key))) {
            error = makeError("CONFIG_INVALID", "Required unified configuration field is missing.", true);
            error.string_details["field"] = std::string(context) + "." + std::string(key);
            return false;
        }
    }
    return true;
}

bool requireStringValue(
    const Json& object,
    const char* key,
    const std::initializer_list<std::string_view> allowed_values,
    ErrorInfo& error,
    const std::string_view context) {
    const auto iterator = object.find(key);
    const std::string field = std::string(context) + "." + key;
    if (iterator == object.end() || !iterator->is_string()) {
        error = makeError("CONFIG_INVALID", "Configuration string field is missing or invalid.", true);
        error.string_details["field"] = field;
        return false;
    }
    for (const auto allowed : allowed_values) {
        if (iterator->get_ref<const std::string&>() == allowed) {
            return true;
        }
    }
    error = makeError("CONFIG_INVALID", "Configuration string field has an unsupported value.", true);
    error.string_details["field"] = field;
    return false;
}

bool requireBooleanValue(
    const Json& object,
    const char* key,
    const bool expected,
    ErrorInfo& error,
    const std::string_view context) {
    const auto iterator = object.find(key);
    const std::string field = std::string(context) + "." + key;
    if (iterator == object.end() || !iterator->is_boolean() || iterator->get<bool>() != expected) {
        error = makeError("CONFIG_INVALID", "Configuration boolean field has an invalid value.", true);
        error.string_details["field"] = field;
        return false;
    }
    return true;
}

bool requireBoolean(
    const Json& object,
    const char* key,
    ErrorInfo& error,
    const std::string_view context) {
    const auto iterator = object.find(key);
    const std::string field = std::string(context) + "." + key;
    if (iterator == object.end() || !iterator->is_boolean()) {
        error = makeError("CONFIG_INVALID", "Configuration boolean field is missing or invalid.", true);
        error.string_details["field"] = field;
        return false;
    }
    return true;
}

bool requireNullablePositiveNumber(
    const Json& object,
    const char* key,
    ErrorInfo& error,
    const std::string_view context) {
    const auto iterator = object.find(key);
    const std::string field = std::string(context) + "." + key;
    if (iterator == object.end() || iterator->is_string() ||
        (!iterator->is_null() && !iterator->is_number())) {
        error = makeError("CONFIG_INVALID", "Configuration value must be a positive number or null.", true);
        error.string_details["field"] = field;
        return false;
    }
    if (!iterator->is_null() &&
        (!std::isfinite(iterator->get<double>()) || iterator->get<double>() <= 0.0)) {
        error = makeError("CONFIG_INVALID", "Configuration value must be finite and positive.", true);
        error.string_details["field"] = field;
        return false;
    }
    return true;
}

bool requirePositiveNumber(
    const Json& object,
    const char* key,
    ErrorInfo& error,
    const std::string_view context) {
    const auto iterator = object.find(key);
    const std::string field = std::string(context) + "." + key;
    if (iterator == object.end() || !iterator->is_number() ||
        !std::isfinite(iterator->get<double>()) || iterator->get<double>() <= 0.0) {
        error = makeError("CONFIG_INVALID", "Configuration value must be finite and positive.", true);
        error.string_details["field"] = field;
        return false;
    }
    return true;
}

bool requireNumberArray(
    const Json& object,
    const char* key,
    ErrorInfo& error,
    const std::string_view context) {
    const auto iterator = object.find(key);
    const std::string field = std::string(context) + "." + key;
    if (iterator == object.end() || !iterator->is_array() || iterator->empty()) {
        error = makeError("CONFIG_INVALID", "Configuration field must be a non-empty number array.", true);
        error.string_details["field"] = field;
        return false;
    }
    for (const auto& item : *iterator) {
        if (!item.is_number() || !std::isfinite(item.get<double>()) || item.get<double>() <= 0.0) {
            error = makeError("CONFIG_INVALID", "Configuration array values must be finite and positive.", true);
            error.string_details["field"] = field;
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
    ErrorInfo& error,
    const RecognitionMode recognition_mode) {
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
    const auto data_source = root.find("data_source");
    if (recognition_mode == RecognitionMode::HartmannMultispotExperimental &&
        data_source != root.end()) {
        if (!data_source->is_string() || data_source->get_ref<const std::string&>().empty()) {
            error = makeError("UNKNOWN_ERROR", "M1 data_source must be a non-empty string when present.", true);
            return false;
        }
        input.data_source = data_source->get<std::string>();
    }

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
    const ProcessingConfig requested = config;
    config = {};
    config.recognition_mode = requested.recognition_mode;
    config.multispot_min_count = requested.multispot_min_count;
    config.multispot_max_count = requested.multispot_max_count;
    config.multispot_min_area_pixels = requested.multispot_min_area_pixels;
    config.multispot_max_area_ratio = requested.multispot_max_area_ratio;
    config.multispot_relative_min_area_ratio = requested.multispot_relative_min_area_ratio;
    config.multispot_fragment_proximity_factor = requested.multispot_fragment_proximity_factor;
    config.multispot_fragment_max_area_ratio = requested.multispot_fragment_max_area_ratio;
    config.multispot_merged_area_ratio = requested.multispot_merged_area_ratio;
    config.multispot_merged_elongation_ratio = requested.multispot_merged_elongation_ratio;
    config.multispot_border_margin_pixels = requested.multispot_border_margin_pixels;
    config.multispot_background_factor = requested.multispot_background_factor;
    config.multispot_min_threshold = requested.multispot_min_threshold;
    config.multispot_min_confidence = requested.multispot_min_confidence;
    config.multispot_16bit_white_level = requested.multispot_16bit_white_level;
    error = {};
    Json root;
    if (!readJson(path, root, error, "CONFIG_NOT_FOUND", "CONFIG_INVALID")) {
        return false;
    }

    std::string schema_version;
    if (!requireAllowedKeys(
            root,
            {"schema_version", "config_name", "camera", "optical", "image_processing", "recognition", "calculation", "path_policy"},
            {"schema_version", "config_name", "camera", "camera_simulation", "optical", "image_processing", "recognition", "calculation", "path_policy", "illumination", "coordinate_system", "hartmann_calibration", "measurement_targets", "data_profile", "calibration_reference"},
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

    if (!requireAllowedKeys(
            recognition,
            {"expected_spot_count", "min_confidence"},
            {"expected_spot_count", "min_confidence", "spot_count_mode"},
            error,
            "recognition") ||
        !requireNumber(image, "roi_width_ratio", config.roi_width_ratio, error, "image_processing") ||
        !requireNumber(image, "roi_height_ratio", config.roi_height_ratio, error, "image_processing") ||
        !requireInteger(image, "median_kernel", config.median_kernel, error, "image_processing") ||
        !requireInteger(image, "tophat_kernel", config.tophat_kernel, error, "image_processing") ||
        !requireNumber(image, "otsu_a", config.otsu_a, error, "image_processing") ||
        !requireNumber(image, "otsu_b", config.otsu_b, error, "image_processing") ||
        !requireInteger(image, "max_depth", config.max_depth, error, "image_processing") ||
        !requireNumber(recognition, "min_confidence", config.min_confidence, error, "recognition")) {
        return false;
    }

    const auto spot_count_mode = recognition.find("spot_count_mode");
    const auto expected_spot_count = recognition.find("expected_spot_count");
    if (spot_count_mode != recognition.end()) {
        if (!requireStringValue(recognition, "spot_count_mode", {"auto", "fixed"}, error, "recognition")) {
            return false;
        }
        if (spot_count_mode->get_ref<const std::string&>() == "auto") {
            if (!expected_spot_count->is_null()) {
                error = makeError("CONFIG_INVALID", "Automatic spot-count mode requires a null expected count.", true);
                error.string_details["field"] = "recognition.expected_spot_count";
                return false;
            }
            config.expected_spot_count = 5;
        } else if (!requireInteger(recognition, "expected_spot_count", config.expected_spot_count, error, "recognition")) {
            return false;
        }
    } else if (!requireInteger(recognition, "expected_spot_count", config.expected_spot_count, error, "recognition")) {
        return false;
    }

    if (config.roi_width_ratio <= 0.0 || config.roi_width_ratio > 1.0 ||
        config.roi_height_ratio <= 0.0 || config.roi_height_ratio > 1.0 ||
        config.median_kernel < 1 || config.median_kernel % 2 == 0 ||
        config.tophat_kernel < 1 || config.tophat_kernel > 4096 ||
        config.otsu_a <= 0.0 || config.otsu_a >= config.otsu_b ||
        config.otsu_b > 1.0 || config.max_depth < 0 || config.max_depth > 8 ||
        (config.recognition_mode == RecognitionMode::FiveSpotCompat &&
         config.expected_spot_count != 5) ||
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

    const auto camera_simulation_iterator = root.find("camera_simulation");
    if (camera_simulation_iterator != root.end()) {
        const Json& simulation = *camera_simulation_iterator;
        int bit_depth = 0;
        if (!requireExactKeys(
                simulation,
                {"parameter_status", "color_mode", "bit_depth", "exposure_min_ms", "exposure_max_ms", "image_plane_width_mm", "image_plane_height_mm"},
                error,
                "camera_simulation") ||
            !requireStringValue(simulation, "parameter_status", {"simulated", "measured"}, error, "camera_simulation") ||
            !requireStringValue(simulation, "color_mode", {"mono"}, error, "camera_simulation") ||
            !requireInteger(simulation, "bit_depth", bit_depth, error, "camera_simulation") ||
            !requirePositiveNumber(simulation, "exposure_min_ms", error, "camera_simulation") ||
            !requirePositiveNumber(simulation, "exposure_max_ms", error, "camera_simulation") ||
            !requirePositiveNumber(simulation, "image_plane_width_mm", error, "camera_simulation") ||
            !requirePositiveNumber(simulation, "image_plane_height_mm", error, "camera_simulation")) {
            return false;
        }
        if (bit_depth != 8 ||
            simulation.at("exposure_min_ms").get<double>() > simulation.at("exposure_max_ms").get<double>()) {
            error = makeError("CONFIG_INVALID", "Camera simulation parameters are outside the supported range.", true);
            error.string_details["field"] = "camera_simulation";
            return false;
        }
        config.max_depth = image.at("max_depth").get<int>();
    }

    const auto illumination_iterator = root.find("illumination");
    if (illumination_iterator != root.end()) {
        const Json& illumination = *illumination_iterator;
        if (!requireExactKeys(illumination, {"source_color", "wavelength_nm"}, error, "illumination") ||
            !requireStringValue(illumination, "source_color", {"green", "green_led"}, error, "illumination") ||
            !requireNullablePositiveNumber(illumination, "wavelength_nm", error, "illumination")) {
            return false;
        }
    }

    const auto coordinate_iterator = root.find("coordinate_system");
    if (coordinate_iterator != root.end()) {
        const Json& coordinate = *coordinate_iterator;
        if (!requireExactKeys(
                coordinate,
                {"coordinate_type", "origin", "x_positive", "y_positive", "y_flip", "confirmation_status"},
                error,
                "coordinate_system") ||
            !requireStringValue(coordinate, "coordinate_type", {"cartesian"}, error, "coordinate_system") ||
            !requireStringValue(coordinate, "origin", {"top_left"}, error, "coordinate_system") ||
            !requireStringValue(coordinate, "x_positive", {"right"}, error, "coordinate_system") ||
            !requireStringValue(coordinate, "y_positive", {"down"}, error, "coordinate_system") ||
            !requireBooleanValue(coordinate, "y_flip", false, error, "coordinate_system") ||
            !requireStringValue(coordinate, "confirmation_status", {"pending_hardware", "confirmed"}, error, "coordinate_system")) {
            return false;
        }
    }

    const auto hartmann_iterator = root.find("hartmann_calibration");
    if (hartmann_iterator != root.end()) {
        const Json& hartmann = *hartmann_iterator;
        if (!requireExactKeys(
                hartmann,
                {"spacing_source", "spot_spacing_px", "spacing_formula"},
                error,
                "hartmann_calibration") ||
            !requireStringValue(hartmann, "spacing_source", {"camera_pixel_spacing"}, error, "hartmann_calibration") ||
            !requireNullablePositiveNumber(hartmann, "spot_spacing_px", error, "hartmann_calibration") ||
            !requireStringValue(
                hartmann,
                "spacing_formula",
                {"spot_spacing_px * camera.pixel_size_um / 1000"},
                error,
                "hartmann_calibration")) {
            return false;
        }
    }

    const auto targets_iterator = root.find("measurement_targets");
    if (targets_iterator != root.end()) {
        const Json& targets = *targets_iterator;
        if (!requireExactKeys(
                targets,
                {"sphere_min_d", "sphere_max_d", "sphere_steps_d", "cylinder_min_d", "cylinder_max_d", "cylinder_steps_d", "prism_min_delta", "prism_max_delta", "prism_step_delta", "axis_min_degree", "axis_max_degree", "axis_step_degree", "addition_min_d", "addition_max_d", "addition_steps_d", "uv_min_percent", "uv_max_percent", "uv_steps_percent"},
                error,
                "measurement_targets")) {
            return false;
        }
        double target_number = 0.0;
        for (const char* key : {"sphere_min_d", "sphere_max_d", "cylinder_min_d", "cylinder_max_d", "prism_min_delta", "prism_max_delta", "prism_step_delta", "axis_min_degree", "axis_max_degree", "axis_step_degree", "addition_min_d", "addition_max_d", "uv_min_percent", "uv_max_percent"}) {
            if (!requireNumber(targets, key, target_number, error, "measurement_targets")) {
                return false;
            }
        }
        if (!requireNumberArray(targets, "sphere_steps_d", error, "measurement_targets") ||
            !requireNumberArray(targets, "cylinder_steps_d", error, "measurement_targets") ||
            !requireNumberArray(targets, "addition_steps_d", error, "measurement_targets") ||
            !requireNumberArray(targets, "uv_steps_percent", error, "measurement_targets")) {
            return false;
        }
        const auto range = [&targets](const char* low, const char* high) {
            return targets.at(low).get<double>() <= targets.at(high).get<double>();
        };
        if (!range("sphere_min_d", "sphere_max_d") || !range("cylinder_min_d", "cylinder_max_d") ||
            !range("prism_min_delta", "prism_max_delta") || !range("axis_min_degree", "axis_max_degree") ||
            !range("addition_min_d", "addition_max_d") || !range("uv_min_percent", "uv_max_percent") ||
            targets.at("axis_min_degree") != 0 || targets.at("axis_max_degree") != 180 ||
            targets.at("prism_step_delta").get<double>() <= 0.0 ||
            targets.at("axis_step_degree").get<double>() <= 0.0 ||
            targets.at("uv_min_percent").get<double>() < 0.0 || targets.at("uv_max_percent").get<double>() > 100.0) {
            error = makeError("CONFIG_INVALID", "Measurement target ranges are invalid.", true);
            error.string_details["field"] = "measurement_targets";
            return false;
        }
    }

    const auto profile_iterator = root.find("data_profile");
    const auto reference_iterator = root.find("calibration_reference");
    if ((profile_iterator == root.end()) != (reference_iterator == root.end())) {
        error = makeError("CONFIG_INVALID", "data_profile and calibration_reference must be provided together.", true);
        return false;
    }
    if (profile_iterator != root.end()) {
        const Json& profile = *profile_iterator;
        const Json& reference = *reference_iterator;
        if (!requireExactKeys(profile, {"data_source", "validation_status", "hardware_parameters_confirmed"}, error, "data_profile") ||
            !requireExactKeys(reference, {"calibration_file", "calibration_version", "parameter_status"}, error, "calibration_reference") ||
            !requireStringValue(profile, "data_source", {"synthetic", "mock", "real"}, error, "data_profile") ||
            !requireStringValue(profile, "validation_status", {"simulation_only", "software_verified", "metrology_validated"}, error, "data_profile") ||
            !requireBoolean(profile, "hardware_parameters_confirmed", error, "data_profile") ||
            !requireStringValue(reference, "parameter_status", {"simulated", "measured"}, error, "calibration_reference")) {
            return false;
        }
        if (!reference.at("calibration_file").is_string() || reference.at("calibration_file").get_ref<const std::string&>().empty() ||
            !reference.at("calibration_version").is_string() || reference.at("calibration_version").get_ref<const std::string&>().empty()) {
            error = makeError("CONFIG_INVALID", "Calibration reference strings must be non-empty.", true);
            error.string_details["field"] = "calibration_reference";
            return false;
        }
        if (profile.at("validation_status") == "metrology_validated" &&
            (profile.at("data_source") != "real" || profile.at("hardware_parameters_confirmed") != true)) {
            error = makeError("CONFIG_INVALID", "Metrology-validated configuration requires real data and confirmed hardware parameters.", true);
            error.string_details["field"] = "data_profile.validation_status";
            return false;
        }
        if (profile.at("hardware_parameters_confirmed") == true && reference.at("parameter_status") != "measured") {
            error = makeError("CONFIG_INVALID", "Confirmed hardware parameters require a measured calibration reference.", true);
            error.string_details["field"] = "calibration_reference.parameter_status";
            return false;
        }
        config.data_source = profile.at("data_source").get<std::string>();
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

bool writeExperimentalMultispotSuccess(
    const std::filesystem::path& path,
    const InputPackage& input,
    const std::string& image_type,
    const ImageAnalysis& analysis,
    const ProcessingConfig& config,
    ErrorInfo& write_error) {
    write_error = {};
    if (!analysis.ok() || input.task_id.empty() ||
        config.multispot_min_count < 1 ||
        config.multispot_max_count < config.multispot_min_count ||
        !std::isfinite(config.multispot_min_confidence) ||
        config.multispot_min_confidence < 0.0 || config.multispot_min_confidence > 1.0 ||
        analysis.diagnostics.image_width <= 0 || analysis.diagnostics.image_height <= 0 ||
        analysis.observations.size() < static_cast<std::size_t>(config.multispot_min_count) ||
        analysis.observations.size() > static_cast<std::size_t>(config.multispot_max_count) ||
        (image_type != "calibration" && image_type != "measurement")) {
        write_error = makeError(
            "COORDINATE_SYSTEM_INVALID",
            "M2 refused to serialize an invalid experimental multispot success result.",
            false);
        return false;
    }

    Json spots = Json::array();
    for (std::size_t index = 0; index < analysis.observations.size(); ++index) {
        const auto& observation = analysis.observations[index];
        if (!std::isfinite(observation.center.x) || !std::isfinite(observation.center.y) ||
            observation.center.x < 0.0 || observation.center.x >= analysis.diagnostics.image_width ||
            observation.center.y < 0.0 || observation.center.y >= analysis.diagnostics.image_height ||
            !std::isfinite(observation.area) || observation.area <= 0.0 ||
            observation.area > static_cast<double>(analysis.diagnostics.image_width) *
                analysis.diagnostics.image_height ||
            !std::isfinite(observation.bounding_box_elongation) ||
            observation.bounding_box_elongation < 1.0 ||
            !std::isfinite(observation.principal_axis_elongation) ||
            observation.principal_axis_elongation < 1.0 ||
            !std::isfinite(observation.mean_intensity) || observation.mean_intensity < 0.0 ||
            observation.mean_intensity > 255.0 ||
            !std::isfinite(observation.peak_intensity) ||
            observation.peak_intensity < observation.mean_intensity || observation.peak_intensity > 255.0 ||
            !std::isfinite(observation.peak_residual_intensity) ||
            observation.peak_residual_intensity < 0.0 || observation.peak_residual_intensity > 255.0 ||
            !std::isfinite(observation.integrated_intensity) || observation.integrated_intensity <= 0.0 ||
            observation.integrated_intensity > observation.area * 255.0 ||
            !std::isfinite(observation.confidence) || observation.confidence < 0.0 ||
            observation.confidence > 1.0) {
            write_error = makeError(
                "COORDINATE_SYSTEM_INVALID",
                "Experimental multispot observation contains an invalid value.",
                false);
            write_error.number_details["detection_id"] = static_cast<double>(index);
            return false;
        }
        spots.push_back({
            {"detection_id", index},
            {"x", observation.center.x},
            {"y", observation.center.y},
            {"confidence", observation.confidence},
            {"area_pixel2", observation.area},
            {"bounding_box_elongation_ratio", observation.bounding_box_elongation},
            {"principal_axis_elongation_ratio", observation.principal_axis_elongation},
            {"mean_intensity_8bit", observation.mean_intensity},
            {"peak_intensity_8bit", observation.peak_intensity},
            {"peak_residual_intensity_8bit", observation.peak_residual_intensity},
            {"integrated_residual_8bit_pixel", observation.integrated_intensity},
            {"quality_flags", observation.quality_flags},
        });
    }

    const auto& diagnostics = analysis.diagnostics;
    if (diagnostics.candidate_count != static_cast<int>(analysis.observations.size()) ||
        diagnostics.raw_candidate_count < 0 || diagnostics.rejected_area_count < 0 ||
        diagnostics.rejected_border_count < 0 ||
        diagnostics.rejected_shape_count < 0 ||
        diagnostics.rejected_proximity_count < 0 ||
        diagnostics.segmentation_source.empty() ||
        diagnostics.centroid_intensity_source.empty() ||
        !std::isfinite(diagnostics.background_intensity) || diagnostics.background_intensity < 0.0 ||
        diagnostics.background_intensity > 255.0 ||
        !std::isfinite(diagnostics.detection_threshold) || diagnostics.detection_threshold < 0.0 ||
        diagnostics.detection_threshold > 255.0) {
        write_error = makeError(
            "COORDINATE_SYSTEM_INVALID",
            "Experimental multispot diagnostics contain an invalid value.",
            false);
        return false;
    }
    const bool is_usable = std::all_of(
        analysis.observations.begin(),
        analysis.observations.end(),
        [&config](const SpotObservation& observation) {
            return observation.confidence >= config.multispot_min_confidence;
        });
    const Json document = {
        {"schema_version", "m2.multispot.experimental.1"},
        {"task_id", input.task_id},
        {"module", "m2_image_recognition"},
        {"status", "ok"},
        {"experimental", true},
        {"contract_status", "proposed"},
        {"data_source", input.data_source},
        {"validation_status", "software_verified"},
        {"validation_scope", "software_only"},
        {"metrology_validated", false},
        {"image_type", image_type},
        {"coordinate_type", "image_pixel"},
        {"spots", std::move(spots)},
        {"quality", {
            {"count_policy", "range"},
            {"min_count", config.multispot_min_count},
            {"max_count", config.multispot_max_count},
            {"detected_count", analysis.observations.size()},
            {"raw_candidate_count", diagnostics.raw_candidate_count},
            {"rejected_area_count", diagnostics.rejected_area_count},
            {"rejected_border_count", diagnostics.rejected_border_count},
            {"rejected_shape_count", diagnostics.rejected_shape_count},
            {"rejected_proximity_count", diagnostics.rejected_proximity_count},
            {"background_intensity_8bit", diagnostics.background_intensity},
            {"detection_threshold_8bit", diagnostics.detection_threshold},
            {"segmentation_source", diagnostics.segmentation_source},
            {"centroid_intensity_source", diagnostics.centroid_intensity_source},
            {"minimum_usable_confidence", config.multispot_min_confidence},
            {"is_usable", is_usable},
            {"warnings", diagnostics.warnings},
        }},
        {"matching", {
            {"status", "not_performed"},
            {"id_scope", "image_local"},
            {"physical_identity_guaranteed", false},
            {"owner_status", "unassigned"},
        }},
        {"error", nullptr},
    };
    return writeJson(path, document, write_error);
}

bool writeExperimentalMultispotError(
    const std::filesystem::path& path,
    const InputPackage* input,
    const std::string& image_type,
    const ErrorInfo& module_error,
    ErrorInfo& write_error) {
    write_error = {};
    if (module_error.empty() || module_error.message.empty() ||
        (image_type != "calibration" && image_type != "measurement")) {
        write_error = makeError(
            "UNKNOWN_ERROR",
            "M2 refused to serialize an invalid experimental multispot error result.",
            false);
        return false;
    }
    const Json document = {
        {"schema_version", "m2.multispot.experimental.1"},
        {"task_id", input == nullptr || input->task_id.empty() ? "unknown" : input->task_id},
        {"module", "m2_image_recognition"},
        {"status", "error"},
        {"experimental", true},
        {"contract_status", "proposed"},
        {"data_source", input == nullptr ? "unknown" : input->data_source},
        {"validation_status", "software_verified"},
        {"validation_scope", "software_only"},
        {"metrology_validated", false},
        {"image_type", image_type},
        {"coordinate_type", "image_pixel"},
        {"matching", {
            {"status", "not_performed"},
            {"id_scope", "image_local"},
            {"physical_identity_guaranteed", false},
            {"owner_status", "unassigned"},
        }},
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
            {"normalization_white_level", diagnostics.normalization_white_level},
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
        {"detection", {
            {"candidate_count", diagnostics.candidate_count},
            {"raw_candidate_count", diagnostics.raw_candidate_count},
            {"rejected_area_count", diagnostics.rejected_area_count},
            {"rejected_border_count", diagnostics.rejected_border_count},
            {"rejected_shape_count", diagnostics.rejected_shape_count},
            {"rejected_proximity_count", diagnostics.rejected_proximity_count},
            {"background_intensity_8bit", diagnostics.background_intensity},
            {"threshold_8bit", diagnostics.detection_threshold},
            {"segmentation_source", diagnostics.segmentation_source},
            {"centroid_intensity_source", diagnostics.centroid_intensity_source},
            {"candidate_limit_exceeded", diagnostics.candidate_limit_exceeded},
        }},
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
        {"parameters", {
            {"save_intermediate", options.save_intermediate},
            {"recognition_mode", options.recognition_mode == RecognitionMode::HartmannMultispotExperimental
                ? "hartmann_multispot_experimental"
                : "five_spot_compat"},
        }},
        {"warnings", warnings},
        {"error", run_error.empty() ? Json(nullptr) : errorToJson(run_error)},
    };
    if (options.recognition_mode == RecognitionMode::HartmannMultispotExperimental) {
        document["experimental"] = true;
        document["contract_status"] = "proposed";
        document["data_source"] = input == nullptr ? "unknown" : input->data_source;
        document["validation_scope"] = "software_only";
    }
    return writeJson(path, document, write_error);
}

}  // namespace focimeter::m2
