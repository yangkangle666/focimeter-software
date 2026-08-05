# M2 真实 JPEG 实验输出样例

本目录只保存 M2 对外置真实 JPEG 运行后生成的实验 JSON，不包含原始 ZIP 或 JPG。样例用于 M3 的 `m2.multispot.experimental.1` 合同和安全拒绝回归，不是跨图匹配成功或计量验证数据。

```text
pair_1/
  spots_calib_multispot.json   候选参考图，27 个 image-local detections
  spots_meas_multispot.json    候选测量图 1，27 个 image-local detections
pair_2/
  spots_calib_multispot.json   同一候选参考图，27 个 image-local detections
  spots_meas_multispot.json    候选测量图 2，27 个 image-local detections
```

使用限制：

- `detection_id` 不是 `spot_id`，两个文件中相同数值不保证同一条物理光线。
- 必须检查 `matching.status=not_performed`、`id_scope=image_local` 和 `physical_identity_guaranteed=false`。
- 点数不一致、缺点、伪点或对称歧义必须由匹配层保守处理，不能按数组下标或最近点直接凑对。
- 两对样例当前均为 `27/27` 点，只表示两张图各自检测到 27 点，不表示这些点已经跨图配对。
- `quality.is_usable=true` 只表示 M2 单图检测可供下游读取，不表示 M3 可以生成处方。
- M3 已实际读取四份 JSON；`pair_1` 和 `pair_2` 均因保留的身份风险证据整组返回 `COORDINATE_SYSTEM_INVALID`。这是预期安全拒绝，不允许删除测量点或只使用可匹配子集继续计算。
- 防绕过回归仅在内存副本中清除 `pair_2` 质量标记；几何层仍只得到 `23/27` 对应并整组拒绝。正式 JSON 未被改写，23 个点也不会被用于处方。
- 数据状态为 `software_verified`、`validation_scope=software_only`、`metrology_validated=false`；不能用于宣称 S/C/A 精度。

详细来源哈希、候选统计和稳定性结果见 `focimeter_system/modules/image_recognition/REAL_JPEG_SOFTWARE_VALIDATION.md`。
