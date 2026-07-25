# M2 联调说明

## M1 给 M2

M2 只读取统一 `input_package.json`，需要 `schema_version`、`task_id`、`module=m1_input_config`、`status=ok`，以及 `data.calibration_image`、`data.measurement_image`、`data.config_path`、`data.run_mode=local_image`。

图片不符合五光斑合同不等于 M2 程序崩溃。M2 应返回标准错误 JSON，例如 `SPOT_COUNT_MISMATCH`，M1 不应通过修改字段或提供宣传图来绕过该错误。

M1 配置中已知的相机宽高必须与标定图、测量图实际尺寸一致；使用临时错误尺寸时 M2 返回 `CONFIG_INVALID`。参数确实未知时应使用统一配置允许的 `null` 或 `TODO_CONFIRM`，不能填写无依据的占位整数。

## M2 给 M3

成功时输出：

- `spots_calib.json`
- `spots_meas.json`

两份文件必须具有相同 `task_id`、`spot_id` 集合和 role 映射。坐标是整张原图的 pixel 坐标，左上角为原点，X 向右，Y 向下。M2 不输出 S/C/A，也不负责 pixel 到 mm 的物理换算。

M3 只能在 M2 CLI 退出码为 `0` 后读取成功 spots。任一图片失败时两份输出都会是错误结果，M3 应停止计算并保留上游错误。

## M2 给 M4

M4 应记录 CLI 退出码，只在进程正常结束且退出码为 `0` 时读取两份 spots。`m2_run_log.json` 可作为辅助审计信号，但当前不是统一契约规定的必需输入。不要看到第一份 spots 文件出现就立即读取，因为两份文件无法跨文件系统原子发布。

M4 可以展示：

- task_id、成功/失败状态；
- 五个 spot 的 `spot_id`、role、x、y 和 confidence；
- `quality.warnings`；
- 标准 error 对象；
- `--save-intermediate` 生成的编号图和诊断 JSON。

M4 不应把 `software_verified` 展示成真实计量通过，也不应把 synthetic 或 AI-generated 图片描述成设备实拍图。

## 固定联调材料

- 成功格式：`data/mock/m2_image_recognition/spots_calib_ok.json`
- 成功格式：`data/mock/m2_image_recognition/spots_meas_ok.json`
- 失败格式：`data/mock/m2_image_recognition/error_spot_count_mismatch.json`
- CLI 输入：`data/mock/m2_image_recognition/synthetic/input_package_uneven.json`
- 合成图片和真值：`data/mock/m2_image_recognition/synthetic/manifest.json`

这些材料用于软件联调，不代表真实设备精度。

## 待负责人统一确认

1. `relative_to_project_root` 的正式根目录。
2. `spot_id` 同物理光线语义写入统一契约。
3. 完整错误码表和“一图失败、双输出错误”的规则。
4. M4 是否把 `m2_run_log.json` 正式作为任务完成 manifest。
5. 完全对称十字的身份锚点方案。
