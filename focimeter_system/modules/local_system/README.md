# M4 本地系统与展示模块

当前任务：第二阶段暂缓正式开发。先阅读接口契约、真实数据报告和 UI/仪器参考；待 M1-M3 第二阶段验证通过后，从最新 `develop` 创建一次性 `task/m4-*` 分支，负责主流程、界面、日志和结果展示，不得重写 M2/M3 算法。

负责串联主流程、展示结果、记录日志、提示异常并导出报告。

## 测试材料

- 前端输入模板：`../../data/mock/m4_local_system/frontend_add_input_template.json`
- M1 输入包：`../../data/mock/m1_input_config/input_package_ok.json`
- M2 光斑结果：`../../data/mock/m2_image_recognition/spots_calib_ok.json`
- M2 光斑结果：`../../data/mock/m2_image_recognition/spots_meas_ok.json`
- M3 计算结果：`../../data/mock/m3_calibration_calculation/result_spherical_ok.json`
- 目标输出：`../../data/mock/m4_local_system/display_output_ok.json`
- 错误样例：`../../data/mock/m4_local_system/error_pipeline_failed.json`

不得重写 M2/M3 算法，只消费标准输出并展示。
