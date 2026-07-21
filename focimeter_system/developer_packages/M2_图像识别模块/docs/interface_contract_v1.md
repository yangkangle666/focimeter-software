# 焦度计四模块统一接口契约 v1

这份文件是四个板块合并前必须共同遵守的接口说明。第一阶段各组可以先不用真实算法，直接使用 `data/mock/` 里的伪造数据测试自己的模块。

## 总原则

- 所有模块输入输出统一使用 JSON。
- 所有 JSON 必须包含 `schema_version`、`task_id`、`module`、`status`。
- 成功时 `status` 固定为 `ok`，失败时固定为 `error`。
- 失败时必须返回统一 `error` 对象，不能只在控制台打印错误。
- 图片路径、配置路径全部使用项目内相对路径，不能写个人电脑绝对路径。
- 图像坐标单位固定为 `pixel`，图像坐标原点固定为左上角，X 向右为正，Y 向下为正。
- 屈光度单位固定为 `D`，角度单位固定为 `degree`。

## 四个模块的数据流

```text
M1 输入与配置模块
  输出 input_package.json
        ↓
M2 图像识别模块
  输入 input_package.json
  输出 spots_calib.json + spots_meas.json
        ↓
M3 标定与计算模块
  输入 spots_calib.json + spots_meas.json + config
  输出 result.json
        ↓
M4 本地系统与展示模块
  输入 input_package + spots + result
  输出 展示数据、日志、报告索引
```

## 统一错误格式

```json
{
  "schema_version": "1.0",
  "task_id": "sample_001",
  "module": "m2_image_recognition",
  "status": "error",
  "error": {
    "code": "SPOT_COUNT_MISMATCH",
    "message": "Expected 5 spots but detected 3.",
    "module": "m2_image_recognition",
    "recoverable": true,
    "details": {}
  }
}
```

## 统一 role 含义

光斑识别输出中 `role` 只能使用以下值：

- `center`：中心光斑。
- `y_positive`：标定坐标系 Y 轴正方向光斑。
- `x_positive`：标定坐标系 X 轴正方向光斑。
- `left_or_negative`：负方向或辅助光斑。
- `other`：其他辅助点。
- `unknown`：暂时无法判断角色。

## 各组测试方式

- M1 组：用 `data/mock/m1_input_config/request_ok.json` 测试，输出格式必须对齐 `input_package_ok.json`。
- M2 组：用 M1 的 `input_package_ok.json` 测试，输出格式必须对齐 `spots_calib_ok.json` 和 `spots_meas_ok.json`。
- M3 组：用 M2 的 `spots_calib_ok.json` 和 `spots_meas_ok.json` 测试，输出格式必须对齐 `result_spherical_ok.json`。
- M4 组：用 M1、M2、M3 的输出结果测试，输出格式必须对齐 `display_output_ok.json`。

## 临时前端添加输入的规则

第一阶段每个板块都可以先做自己的临时前端，用来演示“添加输入材料 -> 运行模块 -> 输出结果”。但是必须遵守：

- 临时前端只负责收集材料，不负责改变接口。
- 前端收集到的材料必须转换成本模块统一 `frontend_add_input_template.json` 格式。
- 模块真正运行时，仍然读取统一 JSON 输入。
- 模块输出必须能直接作为下一个模块输入。

链路固定如下：

```text
M1 输出 input_package_ok.json
  = M2 输入 input_package_ok.json

M2 输出 spots_calib_ok.json + spots_meas_ok.json
  = M3 输入 spots_calib_ok.json + spots_meas_ok.json

M3 输出 result_spherical_ok.json / result_cylindrical_ok.json
  = M4 输入 result_spherical_ok.json / result_cylindrical_ok.json
```

## 不允许擅自修改

- 不允许改字段名，例如不能把 `task_id` 改成 `taskId`。
- 不允许改单位，例如不能把 `pixel_size_um` 改成米。
- 不允许改坐标系方向。
- 不允许失败时只返回字符串，必须返回统一 `error` 对象。
- 不允许模块私自跳过上游输出，例如 M3 不应直接读取原始图片重新识别。
- 不允许因为自己做了临时前端，就改变 JSON 字段。
