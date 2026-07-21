# M3 标定与计算模块开发包

你的任务是：

接收光斑识别结果，建立坐标系，计算镜片结果。

## 你的职责

- 读取 M2 输出的光斑结果
- 建立标定坐标系
- 计算光斑位移
- 输出 S / C / A
- 做质量判断

## 你能做的事

- 坐标转换
- 光斑位移计算
- 球镜/柱镜判断
- 结果可信度判断
- 错误原因返回

## 你不能做的事

- 不要重新做图像识别
- 不要修改输入光斑字段
- 不要改变单位和坐标系定义

## 你要对齐的接口

- 临时前端输入模板：`mock_input/frontend_add_input_template.json`
- 输入样例：`mock_input/spots_calib_ok.json`
- 输入样例：`mock_input/spots_meas_ok.json`
- 输出样例：`mock_output/result_spherical_ok.json`
- 输出样例：`mock_output/result_cylindrical_ok.json`
- 错误样例：`mock_error/error_coordinate_invalid.json`
- 统一配置：`docs/default_config.json`
- 总接口：`docs/interface_contract_v1.md`

## 和前后模块的关系

- 你的输入就是 M2 输出的两个光斑文件。
- 你的输出 `result_spherical_ok.json` 或 `result_cylindrical_ok.json` 必须能直接作为 M4 的输入。
- 不允许自己回头读取原始图片重新识别。

## 固定输出字段

- `schema_version`
- `task_id`
- `module`
- `status`
- `lens_type`
- `result`
- `quality`
- `intermediate`
- `error`

## 最后要求

你输出的结果要能直接给 M4 展示，不要再让 M4 重新算一遍。
