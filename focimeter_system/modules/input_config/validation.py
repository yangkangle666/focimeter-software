from typing import Any, Dict, List, Optional, Tuple

from .errors import CONFIG_INVALID, M1Failure


REQUIRED_CONFIG_FIELDS = {"schema_version", "config_name", "camera", "optical", "image_processing", "recognition", "calculation", "path_policy"}
OPTIONAL_CONFIG_FIELDS = {
    "camera_simulation",
    "coordinate_system",
    "illumination",
    "hartmann_calibration",
    "measurement_targets",
    "data_profile",
    "calibration_reference",
}
SECTION_FIELDS = {
    "camera": {"pixel_size_um", "image_width", "image_height"},
    "camera_simulation": {
        "parameter_status", "color_mode", "bit_depth",
        "exposure_min_ms", "exposure_max_ms",
        "image_plane_width_mm", "image_plane_height_mm",
    },
    "optical": {"distance_m", "hartmann_spacing_mm"},
    "image_processing": {"roi_width_ratio", "roi_height_ratio", "median_kernel", "tophat_kernel", "otsu_a", "otsu_b", "max_depth"},
    "calculation": {"pixel_threshold", "angle_unit", "diopter_unit"},
    "path_policy": {"path_type", "allow_absolute_path"},
    "coordinate_system": {
        "coordinate_type", "origin", "x_positive", "y_positive",
        "y_flip", "confirmation_status",
    },
    "illumination": {"source_color", "wavelength_nm"},
    "hartmann_calibration": {"spacing_source", "spot_spacing_px", "spacing_formula"},
    "measurement_targets": {
        "sphere_min_d", "sphere_max_d", "sphere_steps_d",
        "cylinder_min_d", "cylinder_max_d", "cylinder_steps_d",
        "prism_min_delta", "prism_max_delta", "prism_step_delta",
        "axis_min_degree", "axis_max_degree", "axis_step_degree",
        "addition_min_d", "addition_max_d", "addition_steps_d",
        "uv_min_percent", "uv_max_percent", "uv_steps_percent",
    },
    "data_profile": {
        "data_source", "validation_status", "hardware_parameters_confirmed",
    },
    "calibration_reference": {
        "calibration_file", "calibration_version", "parameter_status",
    },
}

LEGACY_RECOGNITION_FIELDS = {"expected_spot_count", "min_confidence"}
CURRENT_RECOGNITION_FIELDS = {"spot_count_mode", "expected_spot_count", "min_confidence"}
CONFIG_NAMES = {"default_config", "fl800_green_config", "legacy_five_spot_config"}
DATA_SOURCES = {"synthetic", "mock", "real"}
VALIDATION_STATUSES = {"simulation_only", "software_verified", "metrology_validated"}
PARAMETER_STATUSES = {"simulated", "measured"}
LEGACY_PROFILE_WARNING = "CONFIG_PROFILE_LEGACY: provenance and calibration metadata are absent"


def _invalid(message: str) -> M1Failure:
    return M1Failure(CONFIG_INVALID, message)


def _number(value: Any) -> bool:
    return isinstance(value, (int, float)) and not isinstance(value, bool)


def _positive_number_or_null(value: Any) -> bool:
    return value is None or (_number(value) and value > 0)


def _positive_int_or_null(value: Any) -> bool:
    return value is None or (isinstance(value, int) and not isinstance(value, bool) and value > 0)


def _positive_number_list(value: Any) -> bool:
    return isinstance(value, list) and bool(value) and all(_number(item) and item > 0 for item in value)


def validate_config(config: Dict[str, Any]) -> Tuple[List[str], Optional[M1Failure]]:
    if (
        not isinstance(config, dict)
        or not REQUIRED_CONFIG_FIELDS.issubset(config)
        or not set(config).issubset(REQUIRED_CONFIG_FIELDS | OPTIONAL_CONFIG_FIELDS)
    ):
        return [], _invalid("配置必须包含资料包规定的全部字段，且不能包含未知字段。")
    if config["schema_version"] != "1.0" or config["config_name"] not in CONFIG_NAMES:
        return [], _invalid("配置 schema_version 或 config_name 不正确。")
    for name in config.keys() & SECTION_FIELDS.keys():
        allowed = SECTION_FIELDS[name]
        section = config[name]
        if not isinstance(section, dict) or set(section) != allowed:
            return [], _invalid(f"{name} 字段不完整或包含未知字段。")

    camera = config["camera"]
    if not _positive_number_or_null(camera["pixel_size_um"]):
        return [], _invalid("camera.pixel_size_um 必须是正数或 null。")
    if not _positive_int_or_null(camera["image_width"]) or not _positive_int_or_null(camera["image_height"]):
        return [], _invalid("camera.image_width/image_height 必须是正整数或 null。")

    if "camera_simulation" in config:
        simulated = config["camera_simulation"]
        if simulated["parameter_status"] not in {"simulated", "measured"}:
            return [], _invalid("camera_simulation.parameter_status 必须为 simulated 或 measured。")
        if simulated["color_mode"] != "mono":
            return [], _invalid("camera_simulation.color_mode 必须为 mono。")
        if simulated["bit_depth"] != 8:
            return [], _invalid("camera_simulation.bit_depth 当前必须为 8。")
        if not all(
            _number(simulated[field]) and simulated[field] > 0
            for field in ("exposure_min_ms", "exposure_max_ms", "image_plane_width_mm", "image_plane_height_mm")
        ):
            return [], _invalid("camera_simulation 的曝光和像面尺寸必须为正数。")
        if simulated["exposure_min_ms"] > simulated["exposure_max_ms"]:
            return [], _invalid("camera_simulation.exposure_min_ms 不能大于最大曝光时间。")

    optical = config["optical"]
    if not _positive_number_or_null(optical["distance_m"]) or not _positive_number_or_null(optical["hartmann_spacing_mm"]):
        return [], _invalid("optical 参数必须是正数或 null。")

    processing = config["image_processing"]
    if not all(_number(processing[field]) and 0 < processing[field] <= 1 for field in ("roi_width_ratio", "roi_height_ratio")):
        return [], _invalid("ROI 比例必须在 (0, 1] 范围内。")
    if not isinstance(processing["median_kernel"], int) or processing["median_kernel"] <= 0 or processing["median_kernel"] % 2 == 0:
        return [], _invalid("median_kernel 必须是正奇数。")
    if not all(isinstance(processing[field], int) and processing[field] > 0 for field in ("tophat_kernel", "max_depth")):
        return [], _invalid("图像处理尺寸和深度必须是正整数。")
    if not _number(processing["otsu_a"]) or not _number(processing["otsu_b"]) or not 0 <= processing["otsu_a"] < processing["otsu_b"] <= 1:
        return [], _invalid("otsu_a 和 otsu_b 必须满足 0 <= a < b <= 1。")

    recognition = config["recognition"]
    if not isinstance(recognition, dict) or set(recognition) not in {
        frozenset(LEGACY_RECOGNITION_FIELDS),
        frozenset(CURRENT_RECOGNITION_FIELDS),
    }:
        return [], _invalid("recognition 字段不完整或包含未知字段。")
    expected_spot_count = recognition["expected_spot_count"]
    if set(recognition) == CURRENT_RECOGNITION_FIELDS:
        spot_count_mode = recognition["spot_count_mode"]
        if spot_count_mode not in {"auto", "fixed"}:
            return [], _invalid("spot_count_mode 必须为 auto 或 fixed。")
        if spot_count_mode == "auto" and expected_spot_count is not None:
            return [], _invalid("auto 模式下 expected_spot_count 必须为 null。")
        if spot_count_mode == "fixed" and not (
            isinstance(expected_spot_count, int)
            and not isinstance(expected_spot_count, bool)
            and expected_spot_count > 0
        ):
            return [], _invalid("fixed 模式下 expected_spot_count 必须为正整数。")
    elif not (
        isinstance(expected_spot_count, int)
        and not isinstance(expected_spot_count, bool)
        and expected_spot_count > 0
    ):
        return [], _invalid("expected_spot_count 必须是正整数。")
    if not _number(recognition["min_confidence"]) or not 0 <= recognition["min_confidence"] <= 1:
        return [], _invalid("min_confidence 必须在 [0, 1] 范围内。")

    calculation = config["calculation"]
    if not _number(calculation["pixel_threshold"]) or calculation["pixel_threshold"] < 0:
        return [], _invalid("pixel_threshold 必须大于等于 0。")
    if calculation["angle_unit"] != "degree" or calculation["diopter_unit"] != "D":
        return [], _invalid("angle_unit 必须为 degree，diopter_unit 必须为 D。")

    policy = config["path_policy"]
    if policy["path_type"] != "relative_to_project_root" or policy["allow_absolute_path"] is not False:
        return [], _invalid("path_policy 必须禁止绝对路径，并使用项目根目录相对路径。")

    if "coordinate_system" in config:
        coordinates = config["coordinate_system"]
        if (
            coordinates["coordinate_type"] != "cartesian"
            or coordinates["origin"] != "top_left"
            or coordinates["x_positive"] != "right"
            or coordinates["y_positive"] != "down"
            or coordinates["y_flip"] is not False
            or coordinates["confirmation_status"] not in {"pending_hardware", "confirmed"}
        ):
            return [], _invalid("coordinate_system 必须使用左上角原点、X 向右、Y 向下且暂不翻转。")

    if "illumination" in config:
        illumination = config["illumination"]
        if illumination["source_color"] not in {"green", "green_led"} or not _positive_number_or_null(illumination["wavelength_nm"]):
            return [], _invalid("illumination 必须使用 green 或 green_led，wavelength_nm 必须是正数或 null。")

    if "hartmann_calibration" in config:
        hartmann = config["hartmann_calibration"]
        if hartmann["spacing_source"] != "camera_pixel_spacing":
            return [], _invalid("hartmann_calibration.spacing_source 必须为 camera_pixel_spacing。")
        if not _positive_number_or_null(hartmann["spot_spacing_px"]):
            return [], _invalid("hartmann_calibration.spot_spacing_px 必须是正数或 null。")
        if hartmann["spacing_formula"] != "spot_spacing_px * camera.pixel_size_um / 1000":
            return [], _invalid("hartmann_calibration.spacing_formula 不符合像素到毫米换算规则。")

    if "measurement_targets" in config:
        targets = config["measurement_targets"]
        ranges = (
            ("sphere_min_d", "sphere_max_d"),
            ("cylinder_min_d", "cylinder_max_d"),
            ("prism_min_delta", "prism_max_delta"),
            ("axis_min_degree", "axis_max_degree"),
            ("addition_min_d", "addition_max_d"),
            ("uv_min_percent", "uv_max_percent"),
        )
        if any(not _number(targets[low]) or not _number(targets[high]) or targets[low] > targets[high] for low, high in ranges):
            return [], _invalid("measurement_targets 的最小值不能大于最大值。")
        if not (targets["axis_min_degree"] == 0 and targets["axis_max_degree"] == 180):
            return [], _invalid("轴向范围必须为 0 到 180 degree。")
        if not (0 <= targets["uv_min_percent"] <= targets["uv_max_percent"] <= 100):
            return [], _invalid("UV 透过率范围必须在 0 到 100 percent。")
        if not all(
            _number(targets[field]) and targets[field] > 0
            for field in ("prism_step_delta", "axis_step_degree")
        ):
            return [], _invalid("棱镜和轴向步长必须是正数。")
        if not all(
            _positive_number_list(targets[field])
            for field in ("sphere_steps_d", "cylinder_steps_d", "addition_steps_d", "uv_steps_percent")
        ):
            return [], _invalid("球镜、柱镜、下加度和 UV 步长必须是非空正数列表。")

    profile = config.get("data_profile")
    reference = config.get("calibration_reference")
    if (profile is None) != (reference is None):
        return [], _invalid("data_profile 与 calibration_reference 必须同时提供或同时省略。")
    if profile is not None:
        if profile["data_source"] not in DATA_SOURCES:
            return [], _invalid("data_profile.data_source 不受支持。")
        if profile["validation_status"] not in VALIDATION_STATUSES:
            return [], _invalid("data_profile.validation_status 不受支持。")
        if not isinstance(profile["hardware_parameters_confirmed"], bool):
            return [], _invalid("data_profile.hardware_parameters_confirmed 必须为布尔值。")
        if (
            profile["validation_status"] == "metrology_validated"
            and (
                profile["data_source"] != "real"
                or profile["hardware_parameters_confirmed"] is not True
            )
        ):
            return [], _invalid("metrology_validated 仅适用于真实数据且硬件参数已确认的配置。")

        calibration_file = reference["calibration_file"]
        if (
            not isinstance(calibration_file, str)
            or not calibration_file
            or not isinstance(reference["calibration_version"], str)
            or not reference["calibration_version"]
            or reference["parameter_status"] not in PARAMETER_STATUSES
        ):
            return [], _invalid("calibration_reference 字段值无效。")
        if (
            profile["hardware_parameters_confirmed"]
            and reference["parameter_status"] != "measured"
        ):
            return [], _invalid("硬件参数已确认时 calibration_reference.parameter_status 必须为 measured。")

    warnings = []
    for path in ("camera.image_width", "camera.image_height", "optical.hartmann_spacing_mm"):
        section, field = path.split(".")
        if config[section][field] is None:
            warnings.append(f"CONFIG_PARAMETER_PENDING: {path}")
    for path in ("illumination.wavelength_nm", "hartmann_calibration.spot_spacing_px"):
        section, field = path.split(".")
        if section in config and config[section][field] is None:
            warnings.append(f"CONFIG_PARAMETER_PENDING: {path}")
    if profile is None:
        warnings.append(LEGACY_PROFILE_WARNING)
    return warnings, None
