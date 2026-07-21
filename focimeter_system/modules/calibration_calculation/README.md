# M3 标定与计算模块

长期分支：`feature/m3-calc`

M3 只读取 M2 输出的标定/测量光斑 JSON 和统一配置，建立标定坐标系并输出 S/C/A 与质量信息。M3 不读取原始图片，也不重新识别光斑。

统一字段、单位、坐标系和错误对象以 `../../docs/interface_contract_v1.md` 为唯一权威来源。

## 当前实现

- M2 输入、统一配置和 M3 输出的 JSON Schema 校验
- 光斑角色、数量、任务编号、单位和计算就绪参数检查
- 基于相同 `spot_id` 的标定/测量光斑配对
- 中心平移消除、标定坐标系和置信度加权二维变换拟合
- 光焦度矩阵与负柱镜 S/C/A 转换
- 标准镜片校正模型拟合、留出验证和质量门槛
- `simulation_only` 与 `metrology_validated` 模型隔离
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

生产计算默认读取 M3 模块内部的校准模型：

```text
modules/calibration_calculation/calibration_model.json
```

当前仓库没有真实计量验证模型。项目统一 M2 mock 用于验证 JSON 接口，不是带证书值的算法真值数据。将它与无噪声仿真模型组合运行时，会按设计返回 `FIT_RESIDUAL_EXCEEDED`，用于验证统一错误链路：

```powershell
python -m modules.calibration_calculation.algorithm.cli calculate `
  --calibration data/mock/m2_image_recognition/spots_calib_ok.json `
  --measurement data/mock/m2_image_recognition/spots_meas_ok.json `
  --config config/default_config.json `
  --model modules/calibration_calculation/examples/calibration/calibration_model.simulation.json `
  --allow-simulation-model
```

`--allow-simulation-model` 只能用于测试。正式测量必须使用真实标准镜片数据拟合并通过留出验证的 `metrology_validated` 模型。

确定性仿真成功路径由测试动态生成已知 S/C/A 对应的测量坐标：

```powershell
python -m unittest modules.calibration_calculation.tests.test_calculator -v
python -m unittest modules.calibration_calculation.tests.test_algorithm_cli -v
```

标准镜片数据集结构见：

```text
modules/calibration_calculation/examples/calibration/calibration_dataset.example.json
```

拟合命令：

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

测试覆盖统一 mock、Schema、错误输入、坐标拟合、S/C/A 数学转换、校准模型和 CLI 退出码。

## 当前限制

- `optical.distance_m=0.03` 来自统一配置，真实硬件定义仍需项目负责人确认。
- M2 必须保证同一任务中相同 `spot_id` 代表同一条物理光线；M3 不做最近邻重新配对。
- 坐标角色和基向量语义与 `reference_implementation/focimeter_cpp/` 一致；度数计算采用多点光焦度矩阵方案，不是对 `Slens/Clens` 两点公式的逐行移植，物理依据仍需同组审核。
- 当前只有确定性仿真模型，没有真实标准镜片数据，因此不能宣称实物精度合格。
