# M1 配置参数区精简设计

## 背景

M1 的配置编辑器当前把内部来源元数据、固定接口约束和用户可调整参数全部展开为表单控件。第三方使用者容易把来源字段当成需要修改的业务参数，也会被只有一个合法值的下拉框分散注意力。

本次采用已确认的 B 方案“核心参数优先”：压缩界面展示，不改变 M1 的 JSON 接口、后端校验、输出日志或 M1→M2 联调包内容。

## 目标

- 隐藏参数来源和内部标定引用的编辑入口。
- 删除只有一个合法选项的下拉控件。
- 保留真正需要人工调整的相机、图像处理、识别、光学和 FL-800 目标参数。
- 在配置区提供一行固定接口摘要，让第三方知道系统约束但不需要编辑它们。
- 保持 `metrology_validated=false` 和 `usable_for=software_integration` 的状态提示可见。

## 非目标

- 不删除配置 JSON 中的来源字段。
- 不改变 Python 校验规则或 M1 输出包格式。
- 不修改 M2 光斑识别和 M3 光学计算。
- 不把临时 `camera.pixel_size_um=4.8` 或 `hartmann_spacing_mm=1.03391` 改成计量参数。

## 界面设计

### 隐藏区域

配置表单不再渲染以下内部元数据 section：

- `parameter_provenance`
- `data_profile`
- `calibration_reference`

这些字段仍由 `state.config` 保留并随 `config_data` 提交，结果 JSON、日志和 ZIP 联调包继续包含它们。

### 移除的固定控件

以下只有一个合法值或属于接口固定约束的字段不再显示为可编辑控件：

- `calculation.angle_unit`、`calculation.diopter_unit`
- `path_policy.path_type`、`path_policy.allow_absolute_path`
- `coordinate_system.coordinate_type`、`origin`、`x_positive`、`y_positive`、`y_flip`、`confirmation_status`
- `hartmann_calibration.spacing_source`、`spacing_formula`

配置区顶部增加固定接口摘要：`degree · D · 左上原点 · Y 向下 · 项目相对路径`。

### 保留的核心控件

- 相机尺寸、像元尺寸、成像模式、曝光和像面参数
- 光学距离和像素间距
- ROI、滤波、阈值和处理深度
- 光斑数量模式、期望数量和最低置信度
- FL-800 球镜、柱镜、棱镜、轴向、下加度、UV 参数
- 绿光配置和待确认波长

`hartmann_spacing_mm` 仍显示在光学组中，但沿用当前临时派生值说明；它不是物面孔距的计量结论。

## 实现方式

在 `app.js` 中增加界面专用的隐藏 section 集合和固定字段集合。`renderConfigFields()` 先跳过隐藏 section，再跳过固定字段；其余字段沿用现有中文标签、单位、数值转换和 `state.config` 更新逻辑。

在 `index.html` 的步骤 4 中增加固定接口摘要容器。摘要为静态文本，不参与提交。结果页现有数据来源、验证状态、硬件确认、计量验证和可用范围徽章保持不变。

## 数据流与兼容性

```text
完整 default_config.json
        |
        v
前端 state.config 保留全部字段
        |
        +--> 配置区只显示核心字段
        |
        +--> /api/run 提交完整 config_data
        v
M1 后端按原规则校验并生成完整 input_package.json / ZIP
```

上传或加载旧配置时，隐藏 section 和固定字段规则按字段名生效；未知字段仍由后端校验处理，前端不会主动删除它们。

## 错误处理

隐藏字段不参与 UI 编辑，但不会绕过校验。用户修改核心字段导致配置无效时，仍由现有 M1 错误消息阻止运行。固定接口摘要只表达当前协议，不承诺硬件参数已经确认。

## 测试与验收

- 静态测试确认隐藏 section 名称不出现在配置字段渲染结果的可见控件路径中。
- 静态测试确认固定字段不生成 `input` 或 `select` 控件，并确认固定接口摘要存在。
- 静态测试确认核心字段标签和 `state.config` 更新逻辑仍存在。
- 运行完整 Python/Web 测试、JSON 校验、JavaScript 语法检查和 `git diff --check`。
- 通过网页加载默认实图配置，确认步骤 4 更短、步骤 5/6 仍能运行并下载完整联调包。

验收标准：界面只简化展示；运行产生的 `input_package.json`、配置来源、临时参数标记和联调 ZIP 内容与改动前契约保持完整。
