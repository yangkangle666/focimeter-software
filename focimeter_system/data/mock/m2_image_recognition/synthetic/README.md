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
- `failure/missing_spots_3.png`：只有三个光斑。
- `failure/extra_spot_6.png`：包含第六个干扰光斑。
- `failure/merged_spots.png`：左侧光斑与额外亮区粘连。
- `failure/ambiguous_roles.png`：方向角色分布不稳定。
- `failure/ambiguous_pairing_45deg.png`：对称十字在大角度旋转时无法可靠判断物理身份。
- `manifest.json`：已知坐标、变换量和预期结果。

四个 `input_package_*.json` 可直接供 M2 CLI 使用，路径都以 `focimeter_system/` 为项目根目录解析。

## 重新生成

构建 M2 后运行：

```powershell
<build-directory>\Debug\m2_generate_synthetic.exe `
  --output focimeter_system\data\mock\m2_image_recognition\synthetic
```

该命令从仓库根目录执行。生成器使用固定坐标与随机种子，相同版本应得到相同测试语义。重新生成会覆盖同名合成文件，不会访问相机或硬件。
