# 四模块 mock 数据说明

本目录提供四个板块的伪造输入/输出数据，用于第一阶段独立开发和第二阶段合并测试。

## 目录

```text
data/mock/
  m1_input_config/
    request_ok.json
    input_package_ok.json
    error_missing_image.json
  m2_image_recognition/
    input_from_m1.json
    spots_calib_ok.json
    spots_meas_ok.json
    error_spot_count_mismatch.json
  m3_calibration_calculation/
    calculation_input_ok.json
    result_spherical_ok.json
    result_cylindrical_ok.json
    error_coordinate_invalid.json
  m4_local_system/
    system_input_ok.json
    display_output_ok.json
    error_pipeline_failed.json
```

## 怎么用

每个小组先用自己目录里的 mock 输入开发，不需要等其他小组完成。

例如 M2 图像识别组可以先读取：

```text
data/mock/m2_image_recognition/input_from_m1.json
```

然后输出：

```text
outputs/spots/spots_calib.json
outputs/spots/spots_meas.json
```

输出内容先对齐：

```text
data/mock/m2_image_recognition/spots_calib_ok.json
data/mock/m2_image_recognition/spots_meas_ok.json
```

## 合并时的核心判断

只要每个模块能读取上游 JSON、输出下游约定 JSON，就可以合并。模块内部怎么实现可以不同，但外部接口不能变。
