# 四模块 mock 数据

本目录提供四个板块的测试输入、预期输出和错误样例。所有模块共用 `../../config/default_config.json` 和 `../../docs/interface_contract_v1.md`。

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
