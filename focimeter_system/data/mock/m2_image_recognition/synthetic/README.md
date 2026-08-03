# M2 合成测试图

本目录保存可重复生成的五光斑测试材料，用于验证 M2 的软件逻辑。它们不是焦度计实拍图，也不能作为识别精度或计量精度证明。

## 目录

- `calibration/base_5spots.png`：标准五点标定图。
- `measurement/translate_rotate_scale.png`：整体平移、旋转和缩放后的测量图。
- `measurement/rotation_25deg.png`：较大但仍在暂定门限内的旋转。
- `measurement/scale_118pct.png`：单独缩放场景。
- `measurement/anisotropic_xy.png`：X/Y 方向采用不同缩放，模拟需要交给 M3 的各向异性形变。
- `measurement/brightness_only.png`：只降低亮度，用于把亮度鲁棒性与噪声鲁棒性分开验证。
- `measurement/noise_only.png`：只加入固定随机种子噪声。
- `measurement/brightness_noise.png`：亮度降低并加入固定随机种子噪声的测量图。
- `measurement/low_contrast.png`：降低前景与背景灰度差，验证适度低对比度输入。
- `measurement/gaussian_blur.png`：加入固定高斯模糊，验证轻微失焦/扩散扰动。
- `measurement/background_gradient.png`：加入固定背景梯度，验证适度照明不均。
- `measurement/uneven_spots.png`：五点亮度与半径小幅不同，验证适度点间差异。
- `failure/missing_spots_3.png`：只有三个光斑。
- `failure/extra_spot_6.png`：包含第六个干扰光斑。
- `failure/merged_spots.png`：左侧光斑与额外亮区粘连。
- `failure/ambiguous_roles.png`：方向角色分布不稳定。
- `failure/ambiguous_pairing_45deg.png`：对称十字在大角度旋转时无法可靠判断物理身份。
- `failure/blank_dark.png`：近全黑图，预期欠曝诊断并识别失败。
- `failure/blank_bright.png`：近全白图，预期过曝诊断并识别失败。
- `failure/hartmann_array_25.png`：25 点阵列，预期提示可能不是第一阶段五点输入。
- `failure/roi_boundary_clipped.png`：一个光斑落在配置 ROI 外，预期数量不匹配。
- `manifest.json`：已知坐标、变换量和预期结果。

五个 `input_package_*.json` 可直接供 M2 CLI 使用，路径都以 `focimeter_system/` 为项目根目录解析。其中 `input_package_missing.json` 是预期失败样例，其余是成功样例。

## 真值说明

`manifest.json` 顶层 `calibration_spots` 给出五个 `spot_id`、`role` 和标定坐标。成功测量图的真值由该坐标结合对应 case 中的 `scale`、`rotation_degrees`、`translation` 或 `affine_matrix` 唯一计算；只改变亮度、噪声、模糊、背景或半径的 case 不改变几何身份。失败 case 记录预期错误码，带质量诊断的 case 还记录 `expected_warning`。

这些坐标和变换是生成器定义的 synthetic ground truth，只证明软件能否找回已知构造，不代表真实光学系统中的物理真值。

## 重新生成

构建 M2 后运行：

```powershell
<build-directory>\Debug\m2_generate_synthetic.exe `
  --output focimeter_system\data\mock\m2_image_recognition\synthetic
```

该命令从仓库根目录执行。生成器使用固定坐标与随机种子，相同版本应得到相同文件内容。重新生成会覆盖同名合成文件，不会访问相机或硬件。

图片可直接在资源管理器、Visual Studio 或任意 PNG 查看器中打开。建议同时查看 `manifest.json`，否则只能看到图形，无法知道该图的预期结果和构造参数。
