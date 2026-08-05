# M3 标定与计算模块

长期分支：`feature/m3-calc`

M3 只读取 M2 输出的标定/测量光斑 JSON 和统一配置，建立标定坐标系并输出 S/C/A 与质量信息。M3 不读取原始图片，也不重新识别光斑。

统一字段、单位、坐标系和错误对象以 `../../docs/interface_contract_v1.md` 为唯一权威来源。

## 当前实现

- M2 输入、统一配置和 M3 输出的 JSON Schema 校验
- 光斑数量、任务编号、单位和计算就绪参数检查；正式目标支持 LM700 / Hartmann 多光斑输入
- 保留 v1 上游已提供物理 `spot_id` 的输入路径；`role` 作为旧 5 点 mock 的兼容字段，不再作为多光斑必填身份字段
- 支持 M2 实验多光斑输出，经 M3 保守跨图匹配并生成内部物理光线身份后计算
- 实验输入只读取 `detection_id`、坐标和置信度；`detection_id` 始终是图内诊断编号，不会直接改名或复制为 `spot_id`
- 跨图匹配要求所有已检测点形成唯一一一对应；任一点无法赋予唯一物理身份时整组返回 `COORDINATE_SYSTEM_INVALID`，不得使用可匹配子集计算处方
- 未匹配、离格、低置信或带不安全标记的伪点会被拒绝；落在合法晶格位置且没有其他区分信息的伪点无法仅靠几何识别，因此不作“所有伪点均可拒绝”的绝对保证
- 中心平移消除、旧 5 点角色坐标系兼容和置信度加权二维变换拟合；无角色多光斑输入使用全部配对点拟合
- 光焦度矩阵与负柱镜 S/C/A 转换
- 标准镜片校准参数求解、校准集/独立验证集/最终测试集隔离和质量门槛（每个样本均检查 S/C）
- 标定数据清单与实际光斑 JSON 内容的联合版本指纹
- 仿真算法版本与经过计量验证的算法版本隔离
- 统一成功结果和错误 JSON 输出

## 依赖

- Python 3.11 或更高版本
- `numpy>=2,<3`
- `jsonschema>=4.22,<5`

安装 M3 依赖：

```powershell
python -m pip install -r modules/calibration_calculation/requirements.txt
```

## 统一输入与输出

从 `focimeter_system` 目录运行命令。正式测试材料使用项目统一 mock：

```text
data/mock/m2_image_recognition/spots_calib_ok.json
data/mock/m2_image_recognition/spots_meas_ok.json
config/default_config.json
```

上游 M1 输入包引用仓库中真实存在的 `calib_mock_001.jpg` 和 `meas_mock_001.jpg`，并由 `validate_mock_data.py` 实际检查路径后设置 `paths_checked=true`。这两张图仅用于路径接口测试；M3 不读取原始图片，只消费上列统一光斑 JSON，并在结果中传递 `MOCK_DATA_ONLY` 标记。

输出必须符合以下统一样例：

```text
data/mock/m3_calibration_calculation/result_spherical_ok.json
data/mock/m3_calibration_calculation/result_cylindrical_ok.json
data/mock/m3_calibration_calculation/error_coordinate_invalid.json
```

## 接口校验

```powershell
python -m modules.calibration_calculation.validator.cli inputs `
  --calibration data/mock/m2_image_recognition/spots_calib_ok.json `
  --measurement data/mock/m2_image_recognition/spots_meas_ok.json `
  --config config/default_config.json `
  --mode calculation-ready

python -m modules.calibration_calculation.validator.cli result `
  --file data/mock/m3_calibration_calculation/result_spherical_ok.json
```

校验通过时退出码为 0，失败时为 2。校验报告属于开发工具输出，不是传给 M4 的 `result.json`。

## 算法计算

生产计算默认读取 M3 模块内部的校准参数文件：

```text
modules/calibration_calculation/calibration_model.json
```

当前仓库没有经过真实计量验证的算法版本。项目统一 M2 mock 用于验证 JSON 接口，不是带证书值的算法真值数据。将它与无噪声仿真参数组合运行时，会按设计返回 `FIT_RESIDUAL_EXCEEDED`，用于验证统一错误链路：

```powershell
python -m modules.calibration_calculation.algorithm.cli calculate `
  --calibration data/mock/m2_image_recognition/spots_calib_ok.json `
  --measurement data/mock/m2_image_recognition/spots_meas_ok.json `
  --config config/default_config.json `
  --model modules/calibration_calculation/examples/calibration/calibration_model.simulation.json `
  --allow-simulation-model
```

`--allow-simulation-model` 只能用于测试。正式测量必须使用真实标准镜片数据完成“校准集 + 独立验证集 + 最终测试集”流程，并使用经过计量验证的算法版本。内部参数文件为兼容现有格式，仍使用 `model_*` 字段和 `validation_status=metrology_validated` 机器值；该机器值表示算法版本已经通过计量验证，不表示使用了机器学习模型。

确定性仿真成功路径由测试动态生成已知 S/C/A 对应的测量坐标：

```powershell
python -m unittest modules.calibration_calculation.tests.test_calculator -v
python -m unittest modules.calibration_calculation.tests.test_algorithm_cli -v
```

标准镜片数据集结构见：

```text
modules/calibration_calculation/examples/calibration/calibration_dataset.example.json
```

构建校准参数命令（`fit-model` 是为兼容现有脚本保留的命令名）：

```powershell
python -m modules.calibration_calculation.algorithm.cli fit-model `
  --dataset data/calibration/dataset.json `
  --config config/default_config.json `
  --project-root . `
  --output modules/calibration_calculation/calibration_model.json
```

## 测试

```powershell
python validate_mock_data.py
python -m unittest discover -s modules/calibration_calculation/tests -v
```

测试覆盖统一 mock、Schema、错误输入、坐标拟合、S/C/A 数学转换、校准参数和 CLI 退出码。

## 当前限制

- 硬件参数状态：
  - optical.distance_m=0.03 m 为临时开发值，真实距离需硬件结构测量或标准镜片标定反推，当前不用于精度证明。
  - 相机参数已采用网络公开工业相机模拟值（1280×1024, 4.8μm, 6.1×4.9mm, 8bit 黑白），全部标注 SIMULATION_ONLY，后续替换为硬件组提供的真实参数。
  - 光源确认为绿光 LED，标注为临时参数；具体波长、功率、亮度稳定性待硬件组提供后替换。
  - 软件可从图片自动读取 image_width / image_height。
  - 坐标系：硬件回复为笛卡尔坐标，项目统一接口规定原点左上角 Y 向下为正，两者是否一致待确认。当前各组继续按统一接口开发，暂不做 Y 轴翻转。
  - 方案确认：正式目标为 LM700 / Hartmann 多光斑位移场方案，固定 5 点只作为历史 mock 和兼容测试，不再作为最终算法目标。
  - 各组只处理离线图片文件（.tif/.png），不做实时采集。M4 暂不做设备通信。
  - 硬件已送达硬件组，软件组拿到完整数据还需时间。
- 测量目标尚未最终确定：当前统一配置的 `measurement_targets` 参考宁波法里奥 FL-8600/FL-800 竞品指标（球镜 ±25D、柱镜 ±10D、轴位 0°~180°、棱镜 0~15△），仅用于代码范围校验，不代表项目正式性能指标。最终指标以甲方/课程/项目验收要求为准。
- `spot_id` 的硬性语义是“同一条物理光线”，不是检测数组序号。对于 `m2.multispot.experimental.1`，M3 先恢复两图的点阵拓扑并用受约束仿射拟合验证；只有全部检测点形成唯一一一对应并通过全部门限后才生成内部 `spot_id`。
- M3 同时接受合成数据的 `validation_scope=simulation_only` 和真实图软件验证的 `validation_scope=software_only`。M2 的 `quality.is_usable=true` 只代表单图检测可读，不代表可以生成处方。
- 固定的当前 M2 94 点输出具有 180° 和对角镜像拓扑别名，因此已重新定性为安全拒绝夹具；数组乱序和两图独立 `detection_id` 重编号不能解除该歧义。非对称合成全覆盖路径可成功匹配；单侧缺点、等点数缺真点并混入伪点、任意身份阻断质量标记、90/180/270° 或镜像对称、整孔距平移、低置信和残差超限均返回 `COORDINATE_SYSTEM_INVALID`。
- PR #10 的四份真实 27 点 JSON 已进入跨模块回归。两组输入的合同均可读取，但都因保留的身份风险证据按策略安全拒绝；它们不是匹配成功或计量成功样例。
- 统一接口目前没有 `ray_id`/`tracking_id`，M3 不单方面要求或新增字段。当前结果只经过软件和合成数据验证；真实设备成对图片上的物理身份、硬件参数和计量精度仍未验证。
- 坐标角色和基向量语义与 `reference_implementation/focimeter_cpp/` 一致；度数计算采用多点光焦度矩阵方案，不是对 `Slens/Clens` 两点公式的逐行移植，物理依据仍需同组审核。
- 当前只有确定性仿真数据，没有真实标准镜片数据、证书值、序列号和重复测量记录，因此不能宣称算法已通过计量验证或实物精度合格。
- 正式数据必须按镜片 `serial_number` 分区。同一镜片的全部重复测量只能出现在校准集、独立验证集或最终测试集中的一个分区，不得随机拆分。数据格式中的 `partition=train` 是兼容保留值，业务含义为“校准集”，不是机器学习训练集。
