# M1 输入与配置模块

开发分支：`feature/m1-input`

负责接收标定图、测量图和配置参数，完成路径与参数校验，并输出标准 `input_package.json`。

## 测试材料

- 前端输入模板：`../../data/mock/m1_input_config/frontend_add_input_template.json`
- 正常输入：`../../data/mock/m1_input_config/request_ok.json`
- 目标输出：`../../data/mock/m1_input_config/input_package_ok.json`
- 错误样例：`../../data/mock/m1_input_config/error_missing_image.json`

不得实现光斑识别或 S/C/A 计算。输出必须能被 M2 直接读取。
