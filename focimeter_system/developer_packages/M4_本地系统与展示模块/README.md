# M4 本地系统与展示模块开发包

你的任务是：

把前 3 个模块串成一个能看、能用、能演示的本地系统。

## 你的职责

- 读取 M1 / M2 / M3 的标准输出
- 做主流程串联
- 做结果展示
- 做日志输出
- 做报告导出

## 你能做的事

- 主界面
- 工作流控制
- 状态展示
- 错误提示
- 报告入口

## 你不能做的事

- 不要重写 M2 / M3 核心算法
- 不要修改上游 JSON 字段
- 不要自己发明新的接口格式

## 你要对齐的接口

- 临时前端输入模板：`mock_input/frontend_add_input_template.json`
- 输入样例：`mock_input/input_package_ok.json`
- 输入样例：`mock_input/spots_calib_ok.json`
- 输入样例：`mock_input/spots_meas_ok.json`
- 输入样例：`mock_input/result_spherical_ok.json`
- 输出样例：`mock_output/display_output_ok.json`
- 错误样例：`mock_error/error_pipeline_failed.json`
- 统一配置：`docs/default_config.json`
- 总接口：`docs/interface_contract_v1.md`

## 和前面模块的关系

你只负责读取 M1、M2、M3 的标准输出并展示，不负责重新计算。

## 固定输出字段

- `schema_version`
- `task_id`
- `module`
- `status`
- `display`
- `artifacts`
- `pipeline`
- `error`

## 最后要求

你负责的是“总壳子”，最后所有模块都要接到你这里。
