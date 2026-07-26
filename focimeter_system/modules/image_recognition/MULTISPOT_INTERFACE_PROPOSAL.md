# M2 多光斑实验输出接口提案

## 状态

本文件是 M2 的接口提案，不修改 [统一 v1 接口](../../docs/interface_contract_v1.md)。当前输出只用于 `software_verified`、`simulation_only` 的合成数据验证；它不是 M3/M4 已批准的正式公共接口，也不是 LM700 或真实焦度计的计量接口。

## 为什么需要单独的多光斑输出

当前 v1 合同固定描述五个点，每个点有正式 `spot_id` 与 `role`。当前五点链路、M3 实际 schema 和团队确认要求标定图与测量图同 ID 指向同一条物理光线；这项物理身份语义尚未正式写入统一 v1 契约。Hartmann 阵列可能有几十到上百个点，在跨图拓扑匹配、缺点、伪点和局部形变规则尚未批准前，不能把“按行列排序的检测序号”伪装成正式物理身份。

因此当前方案保留默认 `five_spot_compat`，同时用 `--experimental-multispot` 产生完全隔离的检测结果。两条路径不会互相覆盖文件。

## 当前实验文件

```text
<output-dir>/experimental_multispot/
  spots_calib_multispot.json
  spots_meas_multispot.json
  m2_multispot_run_log.json
```

每张图各生成一个 JSON。成功文件的实际字段如下：

```json
{
  "schema_version": "m2.multispot.experimental.1",
  "task_id": "example_task",
  "module": "m2_image_recognition",
  "status": "ok",
  "experimental": true,
  "contract_status": "proposed",
  "data_source": "synthetic",
  "validation_status": "software_verified",
  "validation_scope": "simulation_only",
  "metrology_validated": false,
  "image_type": "calibration",
  "coordinate_type": "image_pixel",
  "spots": [
    {
      "detection_id": 0,
      "x": 420.0,
      "y": 292.0,
      "confidence": 0.98,
      "area_pixel2": 58.0,
      "mean_intensity_8bit": 178.2,
      "peak_intensity_8bit": 245.0,
      "peak_residual_intensity_8bit": 151.0,
      "integrated_residual_8bit_pixel": 4100.0,
      "quality_flags": []
    }
  ],
  "quality": {
    "count_policy": "range",
    "min_count": 12,
    "max_count": 150,
    "detected_count": 25,
    "raw_candidate_count": 25,
    "rejected_area_count": 0,
    "rejected_border_count": 0,
    "background_intensity_8bit": 0.0,
    "detection_threshold_8bit": 8.0,
    "minimum_usable_confidence": 0.35,
    "is_usable": true,
    "warnings": []
  },
  "matching": {
    "status": "not_performed",
    "id_scope": "image_local",
    "physical_identity_guaranteed": false,
    "owner_status": "unassigned"
  },
  "error": null
}
```

失败文件保持同一批实验元数据与 `matching` 声明，但使用 `status: "error"` 和已有的标准 `error` 对象。实验检测不新增或修改统一错误码；需要更细的原因时通过 `error.details` 或 M2 中间诊断表达。当前失败 JSON 不包含成功结果中的 `quality` 对象。

## 字段边界

| 字段 | M2 当前含义 | 调用方不得假设 |
| --- | --- | --- |
| `detection_id` | 单个 JSON 内按 `y` 再按 `x` 排序的稳定检测编号 | 标定与测量中相同值表示同一物理光线 |
| `x`、`y` | 整张图片的像素坐标；左上角原点，X 向右、Y 向下 | 毫米坐标、光学坐标或已标定的位移 |
| `confidence` | 基于面积、形状与信号的内部工程评分，范围 0 到 1 | 经真实样本统计标定的概率或计量可信度 |
| `mean_intensity_8bit`、`peak_intensity_8bit` | 当前归一化 8-bit 灰度图中，候选连通域的均值与峰值 | 原始传感器辐射量、曝光或功率测量值 |
| `peak_residual_intensity_8bit` | 顶帽增强值减检测阈值后的最大非负残差 | 能与原图峰值或原图背景直接相减 |
| `integrated_residual_8bit_pixel` | 每个候选中上述非负残差的像素求和 | 原图积分强度或真实光功率 |
| `quality.background_intensity_8bit`、`detection_threshold_8bit` | 顶帽增强残差域中的边缘背景估计与检测阈值 | 原始灰度图中的背景与阈值 |
| `quality` | 当前图的候选与阈值诊断；`status=ok` 只表示流程完成，只有标定与测量两份结果都满足 `is_usable=true` 才能进入后续计算 | 相机、光阑或镜片的正式标定结果 |
| `matching` | 明确宣告未做跨图最终匹配 | M2 已完成 Hartmann 阵列的物理点配对 |
| `data_source` | 实验输入可选的非空字符串扩展；当前建议值为 `synthetic`、`mock`、`real` 或缺省 `unknown` | 已成为正式 M1 v1 字段，或 `real` 自动代表计量验证通过 |

## M2、M3、M4 分工建议

- **M2**：读取离线图，检测候选光斑，输出整图像素坐标与单点质量诊断；对过暗、过亮、过少、过多、边缘裁剪或可能粘连的候选安全失败。
- **M3**：在接口获批后，根据参考/测量点阵的拓扑、几何模型和真实标定信息建立跨图匹配，计算位移场、波前以及 S/C/A。M3 不能把 `detection_id` 当正式 `spot_id`。
- **M4**：在界面上明确标注“实验性/模拟验证”，可叠加显示检测点、数量、warning 与错误；未获批前不应把这些结果展示为处方或真实测量值。

## 与 v1 的冲突点

1. v1 的 `expected_spot_count=5`、`spot_id` 和角色规则不能表达任意 N 点阵列。
2. 当前五点链路和 M3 的实际消费方式要求跨图同 ID 表示同一物理光线，但该语义尚未正式写入 v1；本阶段多光斑模式只验证检测，未定义阵列匹配合同。
3. 多光斑 `role` 不能私自扩展为 `grid_point` 并假装 v1 已接受；当前实验输出不写 `role`。
4. v1 只有固定的正式结果文件名；实验文件必须留在 `experimental_multispot/`，避免 M3 当前五点读取器误消费。
5. `data_source`、`validation_status`、`validation_scope` 与 `metrology_validated` 是实验元数据，不代表已修改 v1 公共 schema。

当前实现为兼容资料来源标注，接受任意非空 `data_source` 字符串；缺省值为 `unknown`。正式接口升级时应由负责人批准固定枚举。无论 `data_source` 是什么，本阶段都固定 `validation_scope=simulation_only`、`metrology_validated=false`；不能仅把值写成 `real` 就升级验证状态。

## 需要负责人确认的内容

1. 多光斑正式接口的所有者是 M2、M3 还是共同维护？
2. 正式结果是否应同时保存参考图/测量图的全部候选、仅保存匹配后的点，还是两者都保存？
3. 物理身份是由 M3 匹配生成，还是将来由 M2 提供经过批准的匹配模块？
4. 缺点、伪点、饱和点和边缘裁剪点的保留/剔除政策与错误码如何定义？
5. `detection_id` 是否要在正式接口中改名或移除，避免与 `spot_id` 混淆？
6. 坐标是否仍统一使用图像像素左上角坐标，何时引入相机标定后的物理坐标？
7. 真实 Hartmann 阵列的标称孔数、间距、可接受数量范围、ROI、曝光和相机位深是什么？

在上述问题获得批准前，实验输出必须继续保持 `experimental=true`、`contract_status=proposed`、`physical_identity_guaranteed=false`、`owner_status=unassigned`、`metrology_validated=false`。
