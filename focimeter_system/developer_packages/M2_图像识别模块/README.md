# M2 图像识别模块开发包

你的任务是：

输入统一图片包，输出 5 个光斑的统一识别结果。

## 你的职责

- 接收 M1 输出的输入包
- 读取标定图和测量图
- ROI、滤波、增强、二值化、连通域、质心提取
- 输出光斑坐标和置信度

## 你能做的事

- 图像预处理
- 光斑检测
- 轮廓筛选
- 质心输出
- 失败时返回统一错误

## 你不能做的事

- 不要计算 S/C/A
- 不要自己改坐标系定义
- 不要读取个人电脑绝对路径

## 你要对齐的接口

- 临时前端输入模板：`mock_input/frontend_add_input_template.json`
- 输入样例：`mock_input/input_package_ok.json`
- 输出样例：`mock_output/spots_calib_ok.json`
- 输出样例：`mock_output/spots_meas_ok.json`
- 错误样例：`mock_error/error_spot_count_mismatch.json`
- 统一配置：`docs/default_config.json`
- 总接口：`docs/interface_contract_v1.md`

## 和前后模块的关系

- 你的输入 `mock_input/input_package_ok.json` 就是 M1 的输出。
- 你的输出 `mock_output/spots_calib_ok.json` 和 `mock_output/spots_meas_ok.json` 必须能直接作为 M3 的输入。
- 不允许自己重新定义 M1 输入格式。

## 固定输出字段

- `schema_version`
- `task_id`
- `module`
- `status`
- `image_type`
- `coordinate_type`
- `spots`
- `quality`
- `error`

## 最后要求

你的输出必须能被 M3 直接读取，不要再回去读原始图片做二次解释。
