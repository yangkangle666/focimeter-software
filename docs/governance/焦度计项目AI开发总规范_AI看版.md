# 焦度计项目 AI 开发总规范（AI 看版）

> **历史规划说明（2026-08-07）**：本文保留早期固定五点原型阶段的统一格式示例，不是当前仓库状态或多光斑实验接口的权威来源。当前状态、第一步/第二步安排和任务分支流程以仓库根 `README.md` 为准；字段以 `focimeter_system/docs/interface_contract_v1.md` 和模块 README 为准。不得照抄本文旧 JSON 示例或自行切换到旧 `feature/*` 分支。

> 给 AI 开工前阅读。AI 必须先确认成员负责哪个板块，再按该板块规则开发。本文直接规定统一格式，不只说“要统一”。

## AI 开工前必须先问成员

在写任何代码或文档前，AI 必须先问：

1. 你负责哪个板块？只能选择：`M1 输入与配置模块`、`M2 图像识别模块`、`M3 标定与计算模块`、`M4 本地系统与展示模块`。
2. 当前处于哪个阶段？只能选择：`第一阶段模块成品`、`第二阶段合并修 bug`、`第三阶段优化创新`。
3. 你要我先做什么？代码、接口、测试样例、README、调试、报告，还是其他？
4. 你现在有哪些本地文件？标定图、测量图、配置文件、mock 数据分别在哪里？

如果成员没有回答清楚，AI 必须继续追问，不能直接开始。

## 四大模块边界

| 模块 | 允许做 | 必须输出 | 禁止做 |
| --- | --- | --- | --- |
| M1 输入与配置 | 读取图片路径、配置参数、样本列表、任务编号 | 标准输入数据包 input_package.json | 禁止做图像识别、禁止计算 S/C/A |
| M2 图像识别 | ROI、滤波、增强、二值化、连通域、质心 | spots_calib.json 和 spots_meas.json | 禁止计算镜片度数 |
| M3 标定与计算 | 坐标系、光斑位移、镜片类型、S/C/A、质量判断 | result.json | 禁止读取原始图片重新识别 |
| M4 本地系统与展示 | 主流程、界面/命令行、日志、结果展示、导出、异常提示 | 可操作本地系统 | 禁止重写 M2/M3 核心算法 |

## 统一目录结构

建议所有本地模块按下面结构组织，至少目录名称要统一：

```text
focimeter_system/
  config/default_config.json
  data/samples/calibration/
  data/samples/measurement/
  data/mock/
  modules/input_config/
  modules/image_recognition/
  modules/calibration_calculation/
  modules/local_system/
  outputs/images/
  outputs/results/
  outputs/logs/
  outputs/reports/
```

## 统一输入数据包

所有模块接收任务时，统一使用这个输入包结构：

```json
{
  "task_id": "sample_001",
  "calibration_image": "data/samples/calibration/calib_001.png",
  "measurement_image": "data/samples/measurement/meas_001.png",
  "config_path": "config/default_config.json",
  "run_mode": "local_image"
}
```

字段不可改名。路径优先使用相对路径，不能硬编码个人电脑绝对路径。

## 统一配置文件

配置统一使用 `config/default_config.json`，结构如下：

```json
{
  "camera": {
    "pixel_size_um": 4.0,
    "image_width": null,
    "image_height": null
  },
  "optical": {
    "distance_m": 0.03,
    "hartmann_spacing_mm": null
  },
  "image_processing": {
    "roi_width_ratio": 0.9,
    "roi_height_ratio": 0.9,
    "median_kernel": 3,
    "tophat_kernel": 30,
    "otsu_a": 0.4,
    "otsu_b": 0.7,
    "max_depth": 2
  },
  "recognition": {
    "expected_spot_count": 5,
    "min_confidence": 0.7
  },
  "calculation": {
    "pixel_threshold": 1.0,
    "angle_unit": "degree"
  }
}
```

不知道的真实硬件参数写 `null` 或 `TODO_CONFIRM`，不能编造。

## 统一光斑识别输出

M2 必须输出如下格式：

```json
{
  "task_id": "sample_001",
  "status": "ok",
  "image_type": "calibration",
  "coordinate_type": "image_pixel",
  "spots": [
    {
      "spot_id": 0,
      "role": "center",
      "x": 512.34,
      "y": 384.21,
      "confidence": 0.96
    }
  ],
  "quality": {
    "expected_count": 5,
    "detected_count": 5,
    "is_usable": true
  },
  "error": null
}
```

统一 `role` 值：`center`、`y_positive`、`x_positive`、`left_or_negative`、`other`。如果不能判断角色，必须写 `unknown` 并给 warning。

## 统一计算输出

M3 必须输出如下格式：

```json
{
  "task_id": "sample_001",
  "status": "ok",
  "lens_type": "spherical",
  "result": {
    "S": -2.50,
    "C": 0.00,
    "A": null,
    "unit": "D"
  },
  "quality": {
    "is_usable": true,
    "confidence": 0.91,
    "warnings": []
  },
  "intermediate": {
    "coordinate_system_valid": true,
    "shift_unit": "pixel"
  },
  "error": null
}
```

## 统一错误格式

所有模块错误必须返回：

```json
{
  "status": "error",
  "error": {
    "code": "SPOT_COUNT_MISMATCH",
    "message": "Expected 5 spots but detected 3.",
    "module": "image_recognition",
    "recoverable": true
  }
}
```

统一错误码：`IMAGE_NOT_FOUND`、`CONFIG_NOT_FOUND`、`CONFIG_INVALID`、`IMAGE_LOAD_FAILED`、`SPOT_COUNT_MISMATCH`、`CENTROID_FAILED`、`COORDINATE_SYSTEM_INVALID`、`UNIT_MISMATCH`、`CALCULATION_FAILED`、`UNKNOWN_ERROR`。

## 统一单位

| 物理量 | 统一单位 | 字段写法 |
| --- | --- | --- |
| 图像坐标 | pixel | `x`、`y`，并写 `coordinate_type=image_pixel` |
| 像元尺寸 | um | `pixel_size_um` |
| 物理距离 | m | `distance_m` |
| 光阑间距 | mm | `hartmann_spacing_mm` |
| 角度 | degree | `A`、`angle_unit=degree` |
| 屈光度 | D | `S`、`C`、`unit=D` |
| 置信度 | 0 到 1 | `confidence=0.91`，不用百分数 |

## 统一坐标系

图像坐标：左上角为原点，X 向右为正，Y 向下为正，单位 pixel。

标定坐标：中心光斑为原点，`y_positive` 定义 Y 轴正方向，`x_positive` 定义 X 轴正方向。M3 计算前必须说明坐标是否已经从图像坐标转换到标定坐标。

## 统一日志格式

每次运行至少保存：

```json
{
  "task_id": "sample_001",
  "module": "image_recognition",
  "start_time": "2026-07-18 10:00:00",
  "end_time": "2026-07-18 10:00:01",
  "status": "ok",
  "input_files": [],
  "output_files": [],
  "parameters": {},
  "warnings": [],
  "error": null
}
```

## AI 开发时的硬性要求

- 先问成员负责哪个板块，再开始。
- 先问当前阶段，再开始。
- 需要其他模块时，先用 mock 数据。
- 不能擅自改字段名、单位、坐标系、错误码。
- 不确定的硬件参数必须标注 `TODO_CONFIRM`。
- 每次输出都要包含运行方法、输入样例、输出样例、错误样例。
