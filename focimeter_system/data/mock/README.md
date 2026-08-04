# 四模块 mock 数据

本目录提供四个板块的测试输入、预期输出和错误样例。所有模块共用 `../../config/default_config.json` 和 `../../docs/interface_contract_v1.md`。

本目录成功样例统一标注 `MOCK_DATA_ONLY`，只能用于第一阶段接口、算法和联调测试，不能作为真实硬件或计量验证证据。`m1_input_config/input_package_ok.json` 中的 `paths_checked=true` 由 `validate_mock_data.py` 对两张仓库 JPG 和统一配置的实际文件状态进行严格检查。

当前 `calib_mock_001.jpg` 与 `meas_mock_001.jpg` 是内容相同的光学系统示意图，只用于验证图片路径和输入包链路，不是可用于评价 M2 识别精度的真实光斑图。M2/M3 的第一阶段算法联调使用本目录内统一、真实存在的 `spots_calib_ok.json` 与 `spots_meas_ok.json`，并继续保留 `MOCK_DATA_ONLY` 标记。

## 严格数据链

```text
m1_input_config/input_package_ok.json
  -> M2 输入

m2_image_recognition/spots_calib_ok.json
m2_image_recognition/spots_meas_ok.json
  -> M3 输入

m3_calibration_calculation/result_spherical_ok.json
m3_calibration_calculation/result_cylindrical_ok.json
  -> M4 输入
```

每个模块目录中的 `frontend_add_input_template.json` 只用于临时演示前端收集材料，不是新的模块间接口。

开发者应先让模块读取上游 mock 输出，再让实际输出与本模块的 `*_ok.json` 保持同样结构。
