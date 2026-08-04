# M2 真实 JPEG 实验输出样例

本目录只保存 M2 对外置真实 JPEG 运行后生成的实验 JSON，不包含原始 ZIP 或 JPG。样例用于 M3 开发 `m2.multispot.experimental.1` 适配层和跨图匹配测试，不是计量验证数据。

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
- 两对样例当前均为 `27/27` 点，但点数相同不是跨图对应关系证据，仍必须先完成几何匹配。
- 数据状态为 `software_verified`、`validation_scope=software_only`、`metrology_validated=false`；不能用于宣称 S/C/A 精度。

详细来源哈希、候选统计和稳定性结果见 `focimeter_system/modules/image_recognition/REAL_JPEG_SOFTWARE_VALIDATION.md`。
