from typing import Any, Dict, List, Optional, Tuple

from .errors import CONFIG_INVALID, M1Failure


CONFIG_FIELDS = {"schema_version", "config_name", "camera", "optical", "image_processing", "recognition", "calculation", "path_policy"}
SECTION_FIELDS = {
    "camera": {"pixel_size_um", "image_width", "image_height"},
    "optical": {"distance_m", "hartmann_spacing_mm"},
    "image_processing": {"roi_width_ratio", "roi_height_ratio", "median_kernel", "tophat_kernel", "otsu_a", "otsu_b", "max_depth"},
    "recognition": {"expected_spot_count", "min_confidence"},
    "calculation": {"pixel_threshold", "angle_unit", "diopter_unit"},
    "path_policy": {"path_type", "allow_absolute_path"},
}


def _invalid(message: str) -> M1Failure:
    return M1Failure(CONFIG_INVALID, message)


def _number(value: Any) -> bool:
    return isinstance(value, (int, float)) and not isinstance(value, bool)


def _positive_number_or_null(value: Any) -> bool:
    return value is None or (_number(value) and value > 0)


def _positive_int_or_null(value: Any) -> bool:
    return value is None or (isinstance(value, int) and not isinstance(value, bool) and value > 0)


def validate_config(config: Dict[str, Any]) -> Tuple[List[str], Optional[M1Failure]]:
    if not isinstance(config, dict) or set(config) != CONFIG_FIELDS:
        return [], _invalid("配置必须包含资料包规定的全部字段，且不能包含未知字段。")
    if config["schema_version"] != "1.0" or config["config_name"] != "default_config":
        return [], _invalid("配置 schema_version 或 config_name 不正确。")
    for name, allowed in SECTION_FIELDS.items():
        section = config[name]
        if not isinstance(section, dict) or set(section) != allowed:
            return [], _invalid(f"{name} 字段不完整或包含未知字段。")

    camera = config["camera"]
    if not _positive_number_or_null(camera["pixel_size_um"]):
        return [], _invalid("camera.pixel_size_um 必须是正数或 null。")
    if not _positive_int_or_null(camera["image_width"]) or not _positive_int_or_null(camera["image_height"]):
        return [], _invalid("camera.image_width/image_height 必须是正整数或 null。")

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
    if not isinstance(recognition["expected_spot_count"], int) or recognition["expected_spot_count"] <= 0:
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

    warnings = []
    for path in ("camera.image_width", "camera.image_height", "optical.hartmann_spacing_mm"):
        section, field = path.split(".")
        if config[section][field] is None:
            warnings.append(f"CONFIG_PARAMETER_PENDING: {path}")
    return warnings, None
