# M3 标定与计算模块

开发分支：`feature/m3-calc`

负责读取 M2 输出的光斑坐标，建立标定坐标系，计算位移并输出 S/C/A 与质量信息。

## 测试材料

- 前端输入模板：`../../data/mock/m3_calibration_calculation/frontend_add_input_template.json`
- 正式输入：`../../data/mock/m2_image_recognition/spots_calib_ok.json`
- 正式输入：`../../data/mock/m2_image_recognition/spots_meas_ok.json`
- 目标输出：`../../data/mock/m3_calibration_calculation/result_spherical_ok.json`
- 目标输出：`../../data/mock/m3_calibration_calculation/result_cylindrical_ok.json`
- 错误样例：`../../data/mock/m3_calibration_calculation/error_coordinate_invalid.json`

不得重新读取原始图片做识别。结果 JSON 必须能被 M4 直接读取。
