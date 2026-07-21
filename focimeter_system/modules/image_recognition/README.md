# M2 图像识别模块

开发分支：`feature/m2-image`

负责读取 M1 输出的图片路径与配置，完成 ROI、滤波、增强、二值化、连通域和质心提取。

## 测试材料

- 前端输入模板：`../../data/mock/m2_image_recognition/frontend_add_input_template.json`
- 正式输入：`../../data/mock/m1_input_config/input_package_ok.json`
- 目标输出：`../../data/mock/m2_image_recognition/spots_calib_ok.json`
- 目标输出：`../../data/mock/m2_image_recognition/spots_meas_ok.json`
- 错误样例：`../../data/mock/m2_image_recognition/error_spot_count_mismatch.json`

不得计算镜片参数。两个光斑 JSON 必须能被 M3 直接读取。
