# 焦度计本地系统第一阶段工作区

这个目录用于四个板块并行开发，不直接改动原来的 `focimeter/` C++ 答辩依据工程。

## 当前已有内容

- `docs/interface_contract_v1.md`：四个模块统一接口契约。
- `config/default_config.json`：统一默认配置。
- `data/mock/`：四个板块的伪造输入与输出数据。
- `data/samples/`：本地测试图片占位文件。
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

统一配置和接口只维护一份，不在各模块目录复制：

```text
config/default_config.json
docs/interface_contract_v1.md
```
