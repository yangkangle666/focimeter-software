from typing import Any, Dict, List, Optional, Tuple

from .errors import CONFIG_INVALID, M1Failure


CALIBRATION_FIELDS = {
    "schema_version",
    "calibration_version",
    "parameter_status",
    "validation_status",
    "hardware_parameters_confirmed",
    "parameters",
}
PARAMETER_FIELDS = {
    "pixel_pitch_mm",
    "effective_focal_length_mm",
    "distance_m",
    "hartmann_spacing_mm",
    "optical_magnification",
    "power_sign",
    "wavelength_nm",
}
PARAMETER_STATUSES = {"simulated", "measured"}
VALIDATION_STATUSES = {"simulation_only", "software_verified", "metrology_validated"}


def _invalid(message: str) -> M1Failure:
    return M1Failure(CONFIG_INVALID, message)


def _number(value: Any) -> bool:
    return isinstance(value, (int, float)) and not isinstance(value, bool)


def _positive(value: Any) -> bool:
    return _number(value) and value > 0


def _positive_or_null(value: Any) -> bool:
    return value is None or _positive(value)


def validate_calibration(
    calibration: Dict[str, Any], config: Dict[str, Any]
) -> Tuple[List[str], Optional[M1Failure]]:
    if not isinstance(calibration, dict) or set(calibration) != CALIBRATION_FIELDS:
        return [], _invalid("标定文件字段不完整或包含未知字段。")
    parameters = calibration.get("parameters")
    if not isinstance(parameters, dict) or set(parameters) != PARAMETER_FIELDS:
        return [], _invalid("标定 parameters 字段不完整或包含未知字段。")

    reference = config.get("calibration_reference")
    profile = config.get("data_profile")
    if not isinstance(reference, dict) or not isinstance(profile, dict):
        return [], _invalid("当前配置未声明 calibration_reference 或 data_profile。")
    if calibration["schema_version"] != "1.0":
        return [], _invalid("标定 schema_version 必须为 1.0。")
    if calibration["calibration_version"] != reference["calibration_version"]:
        return [], _invalid("标定版本与配置引用不一致。")
    if calibration["parameter_status"] not in PARAMETER_STATUSES:
        return [], _invalid("标定 parameter_status 不受支持。")
    if calibration["parameter_status"] != reference["parameter_status"]:
        return [], _invalid("标定参数状态与配置引用不一致。")
    if calibration["validation_status"] not in VALIDATION_STATUSES:
        return [], _invalid("标定 validation_status 不受支持。")
    if calibration["validation_status"] != profile["validation_status"]:
        return [], _invalid("标定验证状态与配置数据来源声明不一致。")
    if not isinstance(calibration["hardware_parameters_confirmed"], bool):
        return [], _invalid("标定 hardware_parameters_confirmed 必须为布尔值。")
    if calibration["hardware_parameters_confirmed"] != profile["hardware_parameters_confirmed"]:
        return [], _invalid("标定硬件确认状态与配置不一致。")

    for field in ("pixel_pitch_mm", "effective_focal_length_mm", "distance_m"):
        if not _positive(parameters[field]):
            return [], _invalid(f"标定参数 {field} 必须为正数。")
    for field in ("hartmann_spacing_mm", "optical_magnification", "wavelength_nm"):
        if not _positive_or_null(parameters[field]):
            return [], _invalid(f"标定参数 {field} 必须为正数或 null。")
    if not _number(parameters["power_sign"]) or parameters["power_sign"] == 0:
        return [], _invalid("标定参数 power_sign 必须为非零数值。")

    if calibration["validation_status"] == "metrology_validated":
        if (
            calibration["parameter_status"] != "measured"
            or calibration["hardware_parameters_confirmed"] is not True
            or any(
                parameters[field] is None
                for field in ("hartmann_spacing_mm", "optical_magnification", "wavelength_nm")
            )
        ):
            return [], _invalid("metrology_validated 标定必须使用已确认的实测完整参数。")

    warnings = []
    for field in ("hartmann_spacing_mm", "optical_magnification", "wavelength_nm"):
        if parameters[field] is None:
            warnings.append(f"CALIBRATION_PARAMETER_PENDING: parameters.{field}")
    return warnings, None
