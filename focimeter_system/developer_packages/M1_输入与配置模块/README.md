# M1 输入与配置模块开发包

你只需要先完成这一件事：

把“标定图 + 测量图 + 配置文件”整理成统一的 `input_package.json`。

## 你的职责

- 读取标定图片路径
- 读取测量图片路径
- 读取配置文件
- 检查文件是否存在
- 检查参数是否合理
- 输出标准输入包

## 你能做的事

- 文件选择
- 路径检查
- 配置读取
- 生成 `input_package.json`
- 输出失败原因

## 你不能做的事

- 不要做图像识别
- 不要做 S/C/A 计算
- 不要直接改别的模块字段

## 你要对齐的接口

- 临时前端输入模板：`mock_input/frontend_add_input_template.json`
- 输入样例：`mock_input/request_ok.json`
- 输出样例：`mock_output/input_package_ok.json`
- 错误样例：`mock_error/error_missing_image.json`
- 统一配置：`docs/default_config.json`
- 总接口：`docs/interface_contract_v1.md`

## 和下一个模块的关系

你的输出 `mock_output/input_package_ok.json` 必须能直接作为 M2 的输入。不要额外套一层，不要改字段名。

## 固定输出字段

- `schema_version`
- `task_id`
- `module`
- `status`
- `data`
- `quality`
- `error`

## 最后要求

你写出来的模块，后面必须能被 M2 直接读取。
