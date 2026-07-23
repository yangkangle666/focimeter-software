# M3 标定与计算模块

长期分支：`feature/m3-calc`

M3 只读取 M2 输出的标定/测量光斑 JSON 和统一配置，建立标定坐标系并输出 S/C/A 与质量信息。M3 不读取原始图片，也不重新识别光斑。

统一字段、单位、坐标系和错误对象以 `../../docs/interface_contract_v1.md` 为唯一权威来源。

## 当前实现

- M2 输入、统一配置和 M3 输出的 JSON Schema 校验
- 光斑角色、数量、任务编号、单位和计算就绪参数检查
- 基于唯一 `role` 的标定/测量光斑槽位配对，允许每张图独立编号
- 中心平移消除、与 C++ 参考实现一致的 Y 主轴标定坐标系和置信度加权二维变换拟合
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

- 第一阶段尚无硬件结构和实测数据，统一使用配置中的 `optical.distance_m=0.03 m` 进行开发、测试和联调。球镜与柱镜计算均从配置读取该值，不在算法中单独硬编码；取得硬件后再统一测量光阑到传感器的真实距离、完成标定并更新配置。
- M3 使用唯一 `role` 进行槽位配对，`spot_id` 只要求在单张图内唯一。未知/重复角色、反射变换或方向反转会被拒绝。该机制不是物理光线追踪，只适用于 mock 和稳定小位移联调。
- 用于真实设备前，M2 必须提供稳定 `ray_id`/`tracking_id`，或由项目负责人批准跨图匹配协议；没有该条件时不得把 M3 输出用于实物验收。
- 坐标角色和基向量语义与 `reference_implementation/focimeter_cpp/` 一致；度数计算采用多点光焦度矩阵方案，不是对 `Slens/Clens` 两点公式的逐行移植，物理依据仍需同组审核。
- 当前只有确定性仿真数据，没有真实标准镜片数据、证书值、序列号和重复测量记录，因此不能宣称算法已通过计量验证或实物精度合格。
- 正式数据必须按镜片 `serial_number` 分区。同一镜片的全部重复测量只能出现在校准集、独立验证集或最终测试集中的一个分区，不得随机拆分。数据格式中的 `partition=train` 是兼容保留值，业务含义为“校准集”，不是机器学习训练集。
