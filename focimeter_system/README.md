# 焦度计本地系统第一阶段工作区

这个目录用于四个板块并行开发，不直接改动原来的 `focimeter/` C++ 答辩依据工程。

## 当前已有内容

- `docs/interface_contract_v1.md`：四个模块统一接口契约。
- `config/default_config.json`：LM700 / Hartmann 自动多光斑默认配置。
- `config/legacy_five_spot_config.json`：第一阶段五光斑兼容配置。
- `data/mock/`：四个板块的伪造输入与输出数据。
- `data/samples/`：本地测试图片占位文件。
- `data/synthetic/`：确定性生成的多光斑参考图与测量图。
- `data/calibration/`：可替换的标定参数 JSON。
- `modules/`：四个板块的正式开发目录。
- `outputs/`：后续各模块运行时的标准输出目录。
- `validate_mock_data.py`：检查 mock JSON 是否格式正确。

## 推荐开发顺序

1. 先读 `docs/interface_contract_v1.md`。
2. 再读自己负责模块目录下的 mock 数据。
3. 先让自己的模块能读取 mock 输入。
4. 再让自己的模块输出与 mock 输出同结构的 JSON。
5. 第二阶段合并时，只按 JSON 接口合并，不按内部代码合并。

## 四个模块目录

后续代码按下面结构继续补：

```text
modules/
  input_config/
  image_recognition/
  calibration_calculation/
  local_system/
```

第一阶段每个模块可以都有自己的临时演示入口，但核心逻辑必须和演示界面分开。

## M1 一键联调

打开本地 M1 网页后，选择“LM700 / Hartmann 多光斑模拟联调”，系统会填入 M2 已验证的 94 点、1280 x 1024 合成图，配套默认绿光配置和模拟标定文件。运行成功后下载完整 ZIP，M2 解压后以该目录作为 `project_root`，直接读取根目录的 `input_package.json`。

五光斑入口保留为历史兼容测试。两种入口都只处理离线图片；M1 不识别光斑、不计算屈光参数，也不把软件联调结果声明为真实计量验证。

统一配置和接口只维护一份，不在各模块目录复制：

```text
config/default_config.json
docs/interface_contract_v1.md
```
