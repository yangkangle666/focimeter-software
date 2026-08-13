# M1 输入与配置模块

当前集成基线：从最新 `origin/develop` 创建一次性 `task/m1-*` 分支，PR 目标为 `develop`。

当前任务：第二阶段只读复核 12 个真实输入包、配置枚举、路径和 manifest；没有明确 M1 问题时不创建无意义 PR。详见仓库根目录 `docs/TASK_BOARD_PHASE2_20260813.md`。

负责接收参考图、测量图和配置参数，完成路径、配置与标定依赖校验，并输出标准 `input_package.json` 和可直接交给 M2 的完整 ZIP。

## 工作模式

- 默认模式：`spot_count_mode=auto`，使用 `data/mock/m2_image_recognition/synthetic_multispot/` 下经 M2 回归验证的 94 点模拟图。
- 兼容模式：`config/legacy_five_spot_config.json` 固定为 5 个光斑，仅用于第一阶段旧接口测试。
- 数据来源：`synthetic` 表示合成图，`mock` 表示接口模拟，`real` 表示真实硬件文件。
- 验证状态：`simulation_only` 仅完成模拟联调，`software_verified` 表示真实文件已通过软件流程，`metrology_validated` 只能用于真实计量验证完成的配置。

## 标定文件

配置通过 `calibration_reference.calibration_file` 引用标定 JSON。当前默认文件是 `../../data/calibration/simulation_calibration.json`，硬件参数就绪后可替换，但版本、参数状态和验证状态必须与主配置一致。

## 输出内容

运行结果保存在 `../../outputs/results/<task_id>/input_package.json`。网页下载的 ZIP 包含输入 JSON、参考图、测量图、运行时配置、标定 JSON 和使用说明；所有引用文件在打包前会再次检查。

## 测试材料

- 前端输入模板：`../../data/mock/m1_input_config/frontend_add_input_template.json`
- 正常输入：`../../data/mock/m1_input_config/request_ok.json`
- 目标输出：`../../data/mock/m1_input_config/input_package_ok.json`
- 错误样例：`../../data/mock/m1_input_config/error_missing_image.json`

M1 不执行光斑识别，不计算 S/C/A，也不采集实时相机数据。`is_usable=true` 仅表示当前联调包的软件路径和契约可用，不代表真实计量验证完成。
