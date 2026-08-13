# 自动焦度计软件项目

本仓库用于开发自动焦度计的软件部分：读取无镜片参考图和镜片测量图，识别 Hartmann 多光斑，建立跨图物理身份，计算镜片参数，并最终在本地系统中展示结果。

> 当前状态日期：2026-08-13
> 当前功能基线：`develop@447c6aa`
> 本段是开发状态快照。每次开工仍须执行 `git fetch origin --prune` 并重新核对 `origin/develop` 和 PR 状态。

## 当前状态

| 板块 | 状态 | 当前结论 |
| --- | --- | --- |
| M1 输入与配置 | 已合入 `develop` | 输入包、配置、路径检查及本地 Web 输入能力通过测试 |
| M2 图像识别 | 已合入 `develop` | 模拟数据和当前三张真实 JPEG 的多光斑检测通过软件验证 |
| M3 标定与计算 | 已合入 `develop` | 模拟链路和当前两组真实数据的工程模式计算已经跑通 |
| M4 本地系统与展示 | 尚未正式启动 | 等第二步新增真实数据盲测完成后再开始接入 |

最近一次完整验证基线：

- M1：`54/54`
- M2：`17/17`
- M3：`130/130`
- Mock JSON：`43/43`
- Mock 路径：`3/3`
- GitHub M2、M3 CI：通过

模拟数据的 M1 -> M2 -> M3 端到端流程已经跑通。第一批真实数据包含一张公共无镜片参考图和两张不同镜片测量图；两组均完成 `27/27` 测量点匹配。显式 M3 工程模式的当前输出为：

| 数据对 | 镜片类型 | S | C | A |
| --- | --- | ---: | ---: | ---: |
| `pair_1` | 柱镜 | `-4.41852 D` | `-1.76222 D` | `154.564 degree` |
| `pair_2` | 球镜 | `-1.29054 D` | `0 D` | `null` |

这些真实输出固定标记为 `software_verified`、`software_only`、`metrology_validated=false`。默认正式模式仍会按安全策略返回 `COORDINATE_SYSTEM_INVALID`；工程模式结果只用于当前软件联调，不能宣称实物计量精度合格。

## 当前执行安排

这里的“第一步、第二步”是 2026-08-07 确认的当前联调顺序，不是早期文档中的三阶段通用规划。

### 第一步：已有真实数据跑通，已完成

- 使用一张公共无镜片参考图分别配对镜片 1、镜片 2 测量图。
- M1 和 M2 生成有效输入，M3 工程模式输出结果。
- 两组均完成 `27/27` 唯一匹配，M1 -> M2 -> M3 软件链路能够端到端运行。
- 代码没有按现有文件名、坐标、点数或参考 S/C/A 硬编码。

### 第二步：新增真实数据盲测，首轮已运行、阻塞于 M2

新增数据位于 `focimeter_system/data/real/multispot_lens_pairs/real_lens_repeat_set_002/`。已确认的镜片 1～3 各包含一张无镜片参考图、四张镜片保持不动的重复测量图、包装照片和商用焦度计 S/C/A 截图，共 12 个输入包。对应关系不确定的第 4 组未纳入可用数据集。

2026-08-13 已保持代码和识别参数不变完成首轮盲测：M1 `12/12` 通过；M2 `0/12`，全部因背景碎片导致预筛候选数超过安全上限而返回 `SPOT_COUNT_MISMATCH`；M3 因没有有效 M2 输出而未运行。现阶段责任在 M2 候选生成/筛选适配，不是 M3。详细数据和 M2 验收条件见该数据集的 `PHASE2_BLIND_RUN_20260813.md`。

1. 首轮参数不变运行已经完成，保留其诊断数据作为修改前基线。
2. M2 按诊断报告改进通用候选生成或有证据的预筛选，不得只提高候选上限，也不得针对文件名、坐标或镜片写特例。
3. M2 修复后重新运行全部 12 个输入包；只有生成有效 M2 输出后才进入 M3 和 S/C/A 重复性对比。
4. 如果后续全部通过，确认当前实现具有初步泛化能力，随后启动 M4。
5. 如果后续失败，继续根据输入、日志和失败阶段判断属于采集、M1、M2 还是 M3；其他板块不得代改代码。

第二步通过后，按顺序完成 M4 串联与展示、完整本地程序回归、`develop` 稳定化，最后再准备合入 `main` 和公开发布。

## Git 工作流

| 分支 | 用途 |
| --- | --- |
| `main` | 稳定公开版本，当前不作为日常开发起点 |
| `develop` | 当前唯一集成基线和新任务起点 |
| `task/m1-*` | M1 单次任务分支 |
| `task/m2-*` | M2 单次任务分支 |
| `task/m3-*` | M3 单次任务分支 |
| `task/m4-*` | M4 单次任务分支 |

新任务默认从最新 `origin/develop` 创建全新的 `task/...` 分支，PR 目标为 `develop`。不得直接在 `develop` 或 `main` 开发和推送，不得沿用已合并或已关闭的旧任务分支。早期 `feature/m1-input` 等长期分支不再作为默认开发基线，除非负责人为某项任务明确指定。

当前唯一未合并 PR 是历史 Web 平台 PR #7，目标为 `feature/m1-input`；按负责人决定暂不处理，不属于当前 M1 -> M3 联调或第二步盲测范围。

## 模块边界

| 板块 | 职责 | 默认允许修改目录 |
| --- | --- | --- |
| M1 | 输入、配置、路径、任务包 | `focimeter_system/modules/input_config/` |
| M2 | 图片读取、图像处理、光斑检测、检测质量 | `focimeter_system/modules/image_recognition/` |
| M3 | 跨图身份、标定、S/C/A 计算、安全拒绝 | `focimeter_system/modules/calibration_calculation/` |
| M4 | 主流程、界面、日志、结果展示、报告和导出 | `focimeter_system/modules/local_system/` |

成员和 AI 默认只能修改本人模块。`focimeter_system/config/`、`focimeter_system/docs/interface_contract_v1.md`、`focimeter_system/data/`、`.github/`、仓库根目录和其他模块都属于共享或他人区域，必须取得负责人逐文件授权后才能修改。

## 仓库结构

```text
focimeter_system/          四模块正式开发区
docs/                      项目管理、历史规划、答辩和汇报材料
references/                标准、论文和原始项目资料
reference_implementation/  原 C++ 答辩依据工程
local_data/                本机真实图片和参考资料，不进入 Git
```

## 开工前必读

1. 本 README 的“当前状态”和“当前执行安排”。
2. `focimeter_system/docs/interface_contract_v1.md`。
3. `focimeter_system/config/default_config.json`。
4. 本人模块目录内的 `README.md`。
5. 负责人提供的仓库外《焦度计项目成员 AI 交接说明》与本次任务单。

统一接口和统一配置是单一来源。早期治理文档中的固定五点示例、旧分支名称和旧阶段状态只用于历史参考，不得覆盖当前代码、接口契约、根 README 或负责人最新决定。

## 数据链

```text
M1 input_package.json
  -> M2 experimental_multispot/spots_calib_multispot.json
       + experimental_multispot/spots_meas_multispot.json
  -> M3 result JSON
  -> M4 界面、日志和报告（待启动）
```

M2 的 `detection_id` 只在单张图内有效，不能直接改名为物理 `spot_id`。跨图物理身份由 M3 建立；正式模式下任一测量点无法唯一赋予身份时必须整组拒绝，不能只用可匹配子集继续计算。
