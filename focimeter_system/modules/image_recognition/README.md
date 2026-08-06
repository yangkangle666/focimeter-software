# M2 图像识别模块

当前阶段版本：`0.4.0`

当前验证状态：`software_verified`。这表示接口、算法逻辑、错误链路和合成数据测试已经完成软件验证，不表示真实设备识别、光学参数或 S/C/A 计量准确性已经通过。达到 `metrology_validated` 前必须完成真实成对图像、硬件参数和标准镜片验证。

M2 读取 M1 的 `input_package.json`，对标定图和测量图执行同一套图像处理流程。默认的五点兼容模式输出 M3 当前可直接读取的 `spots_calib.json` 和 `spots_meas.json`；实验性的 Hartmann 多光斑模式输出隔离的检测结果，供后续接口升级讨论。本模块不计算 S/C/A，也不连接相机或其他硬件。

## 第三阶段：Hartmann 多光斑准备

本阶段以老师提供的 LM700/Hartmann MATLAB 原型资料为检测思路参考，新增**实验性**多光斑检测路径。它解决的是“从一张离线图里找出任意数量的候选光斑，并给出质量信息”，不是 LM700 实机等价实现，更不是计量验证。

| 模式 | 启用方式 | 输出 | ID 含义 | 对接状态 |
| --- | --- | --- | --- | --- |
| `five_spot_compat` | 默认 | `spots_calib.json`、`spots_meas.json` | 正式 v1 `spot_id`；同任务内代表同一物理光线，受当前五点配对限制约束 | PR #4 的五点兼容基线，M3 当前使用 |
| `hartmann_multispot_experimental` | `--experimental-multispot` | `experimental_multispot/spots_*_multispot.json` | `detection_id` 仅表示单张输出中的稳定排序号，**不是**跨图物理身份 | `proposed`，不属于统一 v1 契约 |

实验模式使用整张图像作为检测区域，执行中值滤波、局部背景残差（顶帽）、边缘背景估计、自动阈值、连通域筛选和强度加权质心。它报告每个点的像素坐标、面积、均值/峰值/积分信号、置信度和质量标记；它不进行跨图最终匹配、不产生正式 `spot_id`、不计算位移场或 S/C/A。

### 真实绿色 JPEG 适配

`0.4.0` 使用一组 `1600 x 1200`、8-bit、三通道绿色光斑 JPEG 完成了软件级验证。原始 ZIP 仍保留在仓库外；最新 `develop` 已将负责人确认的软件联调副本放入 `data/real/multispot_lens_pairs/real_lens_pair_set_001/`，M2 只读使用，不在本次 PR 中新增或修改图片。RGB/JPEG 输入继续用 BT.601 亮度图做分割，以降低绿色通道中压缩碎片造成的伪候选；质心、强度和饱和诊断改用绿色通道。灰度 PNG/TIFF、16 位实验输入和默认五点路径保持原行为。

候选筛选现在分别记录原始连通域、触边、面积异常和形状异常数量。触边候选会被单独剔除并产生 `EDGE_CLIPPED_CANDIDATE_REJECTED` warning，只要剩余候选仍满足数量和质量要求，整张图不再因此失败。`detection_id` 仍按单图内 `(y, x, component_label)` 确定性排序，不具有跨图身份语义。

对于暗心、宽光斑导致的小尺度顶帽漏检，实验路径会用独立的大尺度候选补充，并要求候选信号、重复晶格步长以及相反或正交方向证据同时成立。方格阵列同时支持主邻接步长和约 `sqrt(2)` 倍的对角邻接步长；半格碎片和孤立尘点仍须拒绝。补入点始终保留 `LATTICE_RECOVERED_UNVERIFIED`，不会因为软件恢复成功就被视为物理身份可靠。

算法不包含当前三张 JPEG 的文件名分支、固定坐标或固定点数。现有一张无镜片参考图和两张镜片测量图属于开发样本；后续新增图片应先冻结代码和参数，再作为未参与调参的盲验证数据运行。

真实 JPEG 的统计、重编码稳定性和未验证项见 [REAL_JPEG_SOFTWARE_VALIDATION.md](REAL_JPEG_SOFTWARE_VALIDATION.md)。提供给 M3 的脱敏实验 JSON 位于 `samples/real_jpeg_software_verified/`；其中不含原图，也没有正式 `spot_id`。

两份新增设计文档：

- [MULTISPOT_INTERFACE_PROPOSAL.md](MULTISPOT_INTERFACE_PROPOSAL.md)：未修改统一契约前的实验输出格式、M3/M4 所需信息和负责人待确认项。
- [LM700_MULTISPOT_MIGRATION_NOTES.md](LM700_MULTISPOT_MIGRATION_NOTES.md)：老师资料的检测流程与本实现的对应关系、M2/M3 边界和未验证部分。

## 架构

```text
focimeter_m2.exe（CLI）
        ↓
focimeter_m2_core（静态库）
  ├─ JSON 与相对路径校验
  ├─ 完整统一配置校验与输入/输出路径隔离
  ├─ ROI、灰度、中值滤波、顶帽增强、Otsu 二值化
  ├─ 轮廓筛选与亮度加权质心
  ├─ 标定图角色分配
  ├─ 受约束仿射变换枚举与跨图一一配对
  └─ 结果、错误、质量诊断、日志和中间图输出
```

CLI 只负责参数解析和结果提示，核心算法可被后续 Visual Studio/C++ 工程直接链接。

## 依赖

- Visual Studio 2022，MSVC C++17
- CMake 3.21 或更高版本
- OpenCV 4.x：核心库使用 `core`、`imgproc`、`imgcodecs`；CLI 的 `--show` 额外使用 `highgui`
- nlohmann/json 3.11 或更高版本

依赖位置通过 `OpenCV_DIR`、`CMAKE_PREFIX_PATH` 或 CMake 工具链传入。`CMakeLists.txt` 不包含个人电脑绝对路径。

## CMake 与 Visual Studio

请从仓库根目录执行，并在仓库外创建构建目录。下面的 `<opencv-config-dir>` 和 `<dependency-prefix>` 由本机环境决定：

```powershell
cmake -S focimeter_system/modules/image_recognition `
  -B ../build/m2 `
  -G "Visual Studio 17 2022" -A x64 `
  -DOpenCV_DIR=<opencv-config-dir> `
  -DCMAKE_PREFIX_PATH=<dependency-prefix>

cmake --build ../build/m2 --config Release --parallel
ctest --test-dir ../build/m2 -C Release --output-on-failure
```

也可以在 Visual Studio 2022 中打开本目录的 `CMakeLists.txt`，或打开 CMake 生成的解决方案。

Windows 下 CMake 会把 OpenCV 运行时 DLL 及编解码依赖复制到各可执行文件目录，因此 CTest 不需要手动修改 PATH。正式分发只使用 Release 构建；Debug 运行时依赖 Visual Studio 调试库。

后续 C++/Visual Studio 工程可直接集成静态库：

```cmake
set(M2_BUILD_CLI OFF CACHE BOOL "" FORCE)
set(M2_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(M2_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
add_subdirectory(path/to/focimeter_system/modules/image_recognition)
target_link_libraries(your_target PRIVATE focimeter_m2_core)
```

公共头文件位于 `include/focimeter/m2/`。核心库不解析命令行、不弹窗；需要演示窗口时使用 CLI 的 `--show`，或由上层自行展示保存的中间图。

## CLI

```powershell
focimeter_m2.exe `
  --input <input_package.json> `
  --output <output-directory> `
  --project-root <focimeter_system-directory> `
  --save-intermediate
```

- `--project-root`：M1 相对路径的解析根。当前仓库 mock 需要指向 `focimeter_system/`。
- `--save-intermediate`：保存灰度、增强、二值、编号标注图和结构化诊断 JSON。
- `--experimental-multispot`：显式启用实验性 Hartmann 多光斑检测。它将输出写入 `experimental_multispot/`，不生成或覆盖正式五点结果，也不保证跨图物理身份。
- `--experimental-16bit-white-level <code>`：实验模式读取 16 位容器图片时必填，例如 12 位有效数据使用 `4095`。M2 不根据单张图最大值猜测 10/12/14/16 位，配置值小于图中实际码值时返回 `CONFIG_INVALID`。
- `--show`：先保存最终编号图，再通过 CLI 的 OpenCV 窗口显示；默认不弹窗，便于 M4 与自动测试调用。
- `--help`：显示参数说明。

退出码：`0` 成功，`2` 输入或配置无效，`3` 图像识别或跨图配对失败，`4` 输出写入失败。

实验多光斑样例（仅 synthetic/software_verified）：

```powershell
focimeter_m2.exe `
  --input focimeter_system/data/mock/m2_image_recognition/synthetic_multispot/packages/input_package_94_noisy_gradient.json `
  --output <output-directory> `
  --project-root focimeter_system `
  --experimental-multispot `
  --save-intermediate
```

成功时只会生成以下实验文件，不会生成正式 v1 的 `spots_calib.json` 或 `spots_meas.json`：

```text
experimental_multispot/
  spots_calib_multispot.json
  spots_meas_multispot.json
  m2_multispot_run_log.json
  intermediate/                 使用 --save-intermediate 时存在
```

每个检测项使用 `detection_id`，它只是按 `y`、再按 `x` 排序得到的**单图局部编号**。`matching.physical_identity_guaranteed=false` 是硬性声明：M3 或未来经批准的 M2 匹配模块必须建立跨图对应关系，不能把两个文件里同号的 `detection_id` 当成同一条物理光线。

实验 JSON 的 `quality` 还会报告 `raw_candidate_count`、`rejected_border_count`、`rejected_area_count`、`rejected_shape_count`、`rejected_proximity_count`、`segmentation_source` 和 `centroid_intensity_source`。各 `rejected_*_count` 是规则命中次数，分类不保证互斥，不能与原始候选数做简单守恒相加。绿色信号明显弱于亮度信号时，质心权重会从绿色通道回退为 BT.601 亮度并报告 `GREEN_CHANNEL_SIGNAL_WEAK`。每个 spot 的 `bounding_box_elongation_ratio` 与 `principal_axis_elongation_ratio` 分别描述轴对齐包围框和与方向无关的主轴伸长率；这些字段用于诊断异常形状及可能粘连，不是已批准的公共 v1 字段。

实验输出中的 `quality.is_usable` 只有在全部候选达到内部 `minimum_usable_confidence` 时才为 `true`；它只表示 M2 单图检测可供诊断或匹配层读取，不表示跨图物理身份已经建立，也不授权 M3 生成处方。低于该门限的点仍可保留用于诊断，但带有 `LOW_CONFIDENCE` 标记，调用方不得把它当作可直接计算的数据。该置信度是同一顶帽残差域中的信号、形状和面积工程评分，不是统计概率。

## 输入与输出

正式输入来自 M1。仓库当前 M1 mock 输入只验证包结构和路径，引用的 JPG 不是五光斑成功图；可实际跑通识别的 synthetic 示例为：

```text
../../data/mock/m2_image_recognition/synthetic/input_package_uneven.json
```

输出固定写入指定目录：

```text
spots_calib.json
spots_meas.json
m2_run_log.json
.focimeter_m2.lock     内部单写者锁文件，不是业务结果
intermediate/          使用 --save-intermediate 时存在
```

`intermediate/` 中每张图对应可获得的 `*_gray.png`、`*_enhanced.png`、`*_binary.png`、`*_spots.png` 和始终可写时生成的 `*_diagnostics.json`。成功和图像识别失败都会保留已经完成的中间阶段；图片不存在等早期失败没有可用图像，因此只生成诊断 JSON。诊断 JSON 记录图片尺寸、通道、源位深、归一化 8 位 ROI 的灰度统计、候选数和启发式 warning，并明确标记 `validation_status=software_verified`、`metrology_validated=false`。它是调试产物，不属于统一 spots 契约。

每个 `task_id` 必须使用独立输出目录。M2 会锁定该目录并拒绝第二个并发写者；输入包、统一配置和两张图片也不得与上述输出文件重叠。

M4/M3 调用方必须等待 CLI 正常结束，并且仅在退出码为 `0` 时读取两份 spots。成功运行中 `m2_run_log.json` 最后发布，可作为辅助审计信号，但它尚未进入统一接口；不要通过监视某一份 spots 文件出现就提前读取另一份。

输出字段严格对齐：

```text
../../docs/interface_contract_v1.md
../../data/mock/m2_image_recognition/spots_calib_ok.json
../../data/mock/m2_image_recognition/spots_meas_ok.json
```

坐标为整张原图像素坐标：左上角原点，X 向右，Y 向下，`coordinate_type=image_pixel`。ROI 偏移已经加回输出坐标。

## `spot_id` 保证

同一任务中，相同 `spot_id` 必须表示同一条物理光线：

1. 标定图先确定中心和四个方向角色。
2. 测量图不独立排序编号。
3. 程序枚举五点的一一对应关系，并为每个候选拟合保持方向的仿射变换，允许柱镜造成的 X/Y 不同缩放。
4. 只有主轴缩放、整体旋转、全局残差、逐点残差、中心残差和唯一性门限都满足时，才继承标定图的 `spot_id` 与 `role`。
5. 缺点、多点、粘连、残差过大或多种配对同样合理时返回错误，不猜测编号。

当前工程按负责人选择的方案 C 实现：只接受规范化旋转角不超过 `35` 度、仿射主轴缩放在 `0.70` 到 `1.30` 之间且唯一性检查通过的候选，能直接检测到的身份不确定情况一律报错。但是，负责人已确认硬件侧暂时无法给出真实相对旋转范围；完全对称的十字存在 90 度周期的几何别名，仅凭五个坐标无法判断某个小角度候选是否由更大真实旋转折叠而来。仓库外 `+60` 度诊断探针已证实当前实现会将其解释为约 `-30` 度并错误成功。因此，本版本只用于负责人代码审查和已声明支持范围内的合成逻辑验证，尚未无条件满足真实物理光线身份保证。正式实物链路必须增加非对称标记、稳定外观特征或硬件身份锚点，之后才能关闭该限制。

## 合成测试材料

目录：

```text
../../data/mock/m2_image_recognition/synthetic/
```

其中包含平移、旋转、缩放、亮度、噪声、低对比度、模糊、背景梯度、适度光斑差异、缺失、多点阵列、ROI 边界、粘连和角色歧义案例，以及 `manifest.json` 真值说明。它们只用于软件逻辑验证。

仓库原有两张 `data/samples/*.jpg` 是同一张光学系统示意图，不是五光斑照片，运行 M2 时应返回识别错误。

第三阶段多光斑合成数据位于：

```text
../../data/mock/m2_image_recognition/synthetic_multispot/
```

它包含 25 点和 94 点规则阵列、全局平移、局部形变、独立亮度变化、低对比度、背景梯度和噪声，以及缺失、过多、粘连、边缘裁剪、全暗和全亮失败样例。`manifest.json` 中的 `synthetic_point_id` 和已知中心只服务于测试真值核对，不能作为正式 `spot_id` 或真实物理光线证据。所有图片均为可重复生成的 PNG，不是 LM700 实拍图。

## 测试

```powershell
ctest --test-dir <build-directory> -C Debug --output-on-failure
python focimeter_system/validate_mock_data.py
```

测试覆盖：

- 五点检测、原图坐标恢复、置信度范围、8 位/16 位输入和非法配置；
- 测量点输入顺序变化后的稳定配对；
- 平移、旋转、等比例缩放、各向异性仿射形变、亮度和噪声；
- 低对比度、轻度高斯模糊、背景梯度、适度光斑亮度和半径变化；
- 欠曝、过曝、25 点 Hartmann-like 阵列和 ROI 边界诊断；
- 三点、六点、粘连和角色歧义；
- 缺失光斑由伪点补足、逐点/中心残差和配对后置信度失败；
- 输入 JSON 不存在/损坏/含非标准注释、字段缺失、上游错误、绝对路径和父目录穿越；
- 统一配置缺段、额外字段、错误单位/路径策略、非标准注释或算法参数无效；
- 配置缺失、图片缺失/无法解码、输入输出路径重叠、并发写者和输出写入失败；
- 旧成功结果失效和双错误输出；
- RGB 绿色 JPEG、JPEG 二次编码坐标偏差、过曝核心/暗心、尘点、小面积碎片和非致命触边剔除；
- 仓库两组真实 JPEG 输入包的 CLI 软件回归，验证触边 warning 非致命、镜片一不因近圆大连通域误报粘连；
- 方格阵列主邻接/对角邻接恢复、半格干扰和孤立尘点拒绝；
- 三张真实 JPEG 各连续三次的检测顺序、坐标、置信度和质量标记确定性；
- CLI 帮助和端到端输出。

## 错误处理

错误会写入统一 `error` 对象。任一图失败后，两张 spots 输出都标记为错误，避免 M3 使用一半成功但无法配对的数据。当前使用项目总规范已有错误码，例如：

- `IMAGE_NOT_FOUND`
- `IMAGE_LOAD_FAILED`
- `CONFIG_NOT_FOUND`
- `CONFIG_INVALID`
- `SPOT_COUNT_MISMATCH`
- `CENTROID_FAILED`
- `COORDINATE_SYSTEM_INVALID`
- `UNKNOWN_ERROR`

当识别失败时，现有错误对象的 `details` 会附带可获得的图像诊断数据。曝光、对比度和“可能是整张阵列”的判断只提供 warning，不新增统一错误码，也不替代五光斑识别结果。当前启发式阈值未经真实设备标定。

## 第二阶段联调与真实数据准备

- [REAL_DATA_REQUIREMENTS.md](REAL_DATA_REQUIREMENTS.md)：硬件图片、相机参数、光学参数和计量升级条件。
- [INTEGRATION_NOTES_M2_M3_M4.md](INTEGRATION_NOTES_M2_M3_M4.md)：M1 输入、M3 消费、M4 完成边界和展示建议。
- `data/mock/m2_image_recognition/spots_calib_ok.json` 与 `spots_meas_ok.json`：M3/M4 固定成功格式样例。
- `data/mock/m2_image_recognition/error_spot_count_mismatch.json`：固定失败格式样例。
- `data/mock/m2_image_recognition/synthetic/input_package_uneven.json`：可直接运行的第二阶段合成 CLI 样例。

## 当前限制与 TODO_CONFIRM

1. 已收到一张候选参考图和两张候选测量图的 JPEG 副本，并完成单图检测软件验证；没有相机原始文件、采集参数、阵列标定表、标准镜片证书或重复采集数据，因此不能评价物理匹配正确率和计量精度。
2. M1 补充 bundle 的配置声明 `12 x 12`，实际 TIFF 为 `1280 x 1024`，当前先返回 `CONFIG_INVALID`；M1 修正配置后，这组 AI 生成的整张阵列类图片仍不是第一阶段五光斑成功样例，预期继续返回 `SPOT_COUNT_MISMATCH`。两种结果都不能当作真实识别成功。
3. 仓库没有正式定义 `relative_to_project_root` 的根目录；当前按 `focimeter_system/` 解释，负责人需确认。
4. `spot_id` 跨图同物理光线要求来自 M3 实际实现和团队确认，尚未写入统一接口契约。
5. 完整 M2 错误码表和“一图失败时双输出”的规则尚未写入统一接口契约。
6. 配对门限是保守工程参数，需用真实成对图像重新标定。
7. `confidence` 是由圆度、亮度、面积一致性和配对残差组成的工程评分，不是经过统计标定的概率。
8. 当前没有设置输入 JSON、图像文件字节数和解码后像素总量上限；正式接入不受信任的上传内容前应补充资源限制。
9. 同一输出目录通过锁强制只允许一个 M2 进程写入；M4 并发任务必须为每个 `task_id` 分配独立输出目录。
10. 标定角色目前以方向角和中心/半径稳定性判定，尚未用真实设备数据标定更严格的对边、正交和半径一致性门限。
11. 当前二值化使用原半成品思路改造后的区域自适应 Otsu；真实不均匀照明样例到位后仍需重新校准参数。
12. M3 当前 validator 的 `calculation-ready` 模式已经检查两个文件的 `spot_id` 集合和同 ID role 映射，三组合成输出共 `6/6` 次验证通过；该检查能发现接口不一致，但不能独立证明同 ID 在真实图像中确实对应同一条物理光线。
13. 两份 spots 是两个独立文件，无法在普通文件系统上完成一次跨文件原子重命名；正常返回路径会回滚第二次发布失败，但进程被强制终止时仍可能留下半组文件。调用方必须等待 CLI 正常结束且退出码为 `0`，运行日志只作为可选辅助审计信号。
14. `TODO_CONFIRM`：负责人已选择方案 C，并要求身份不确定时报错；硬件侧暂时无法确认真实相对旋转范围。当前实现能拒绝直接超过 `35` 度的候选和已检测到的多解，但无法识别完全对称十字的 90 度隐藏别名；仓库外 `+60` 度探针当前会错误成功。负责人审查后仍需决定采用非对称标记、稳定外观特征还是硬件身份锚点，不能仅调整角度阈值后宣称问题已解决。
15. 图像诊断的灰度统计基于转换后的 8 位 ROI；对于 16 位原图，这些数值不能替代原始传感器曝光分析。
16. 当前顶帽核来自统一配置并能处理本次 JPEG 样本。合成测试已表明光斑直径接近或大于核尺寸时可能被削弱，仍需用更多曝光、焦度和设备状态覆盖的数据重新标定。
17. 标定图和测量图必须具有相同像素尺寸；否则两个文件的像素坐标不能直接配对，M2 返回 `COORDINATE_SYSTEM_INVALID`。
18. 统一配置中的相机宽高若为已知整数，必须与两张解码图片一致；不一致返回 `CONFIG_INVALID`。使用 `null` 或 `TODO_CONFIRM` 时只执行两图彼此尺寸一致性检查。
19. 多光斑检测目前使用连通域而不是老师网格原型中的局部极大值。邻近候选只有在面积明显更小且满足以下证据之一时才作为碎片剔除：两中心之间不存在低于较弱峰值相对背景 50% 的深谷；或中心距离不超过图内中位最近邻间距的 20%。第二条是面向规则 Hartmann 阵列的内部启发式，命中但缺少亮桥时会报告 `SUBPITCH_FRAGMENT_REJECTED_UNVERIFIED`；其物理正确性尚无人工逐点真值验证。其他证据不足的候选会保留并报告 `NEARBY_CANDIDATE_UNRESOLVED`。紧密多峰、严重粘连及极端局部阵列压缩仍缺少真实覆盖。
20. 多光斑数量、相对面积、信号触边判定和置信度目前是 M2 内部实验参数，未写入统一配置；本次参数只经过一组 JPEG 与合成矩阵验证，扩大设备或曝光范围前应形成配置提案。
21. 多光斑匹配所有者尚未由负责人批准，实验 JSON 使用 `matching.owner_status=unassigned`。当前建议由 M3 或未来独立匹配层处理，不能把该建议当作既定公共接口。
