# M2 图像识别模块

当前阶段版本：`0.1.0`

M2 读取 M1 的 `input_package.json`，对标定图和测量图执行同一套图像处理流程，输出 M3 可直接读取的 `spots_calib.json` 和 `spots_meas.json`。本模块不计算 S/C/A，也不连接相机或其他硬件。

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
  └─ 结果、错误、日志和中间图输出
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
- `--save-intermediate`：保存灰度、增强、二值和编号标注图。
- `--show`：先保存最终编号图，再通过 CLI 的 OpenCV 窗口显示；默认不弹窗，便于 M4 与自动测试调用。
- `--help`：显示参数说明。

退出码：`0` 成功，`2` 输入或配置无效，`3` 图像识别或跨图配对失败，`4` 输出写入失败。

## 输入与输出

正式输入为 M1 输出，成功示例：

```text
../../data/mock/m1_input_config/input_package_ok.json
```

输出固定写入指定目录：

```text
spots_calib.json
spots_meas.json
m2_run_log.json
.focimeter_m2.lock     内部单写者锁文件，不是业务结果
intermediate/          使用 --save-intermediate 时存在
```

每个 `task_id` 必须使用独立输出目录。M2 会锁定该目录并拒绝第二个并发写者；输入包、统一配置和两张图片也不得与上述输出文件重叠。

M4/M3 调用方必须等待 CLI 结束，并且仅在退出码为 `0` 时读取两份 spots。成功运行中 `m2_run_log.json` 最后发布，可作为本次运行完成的辅助信号；不要通过监视某一份 spots 文件出现就提前读取另一份。

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

其中包含平移、旋转、缩放、亮度、噪声、缺失、额外光斑、粘连和角色歧义案例，以及 `manifest.json` 真值说明。它们只用于软件逻辑验证。

仓库原有两张 `data/samples/*.jpg` 是同一张光学系统示意图，不是五光斑照片，运行 M2 时应返回识别错误。

## 测试

```powershell
ctest --test-dir <build-directory> -C Debug --output-on-failure
python focimeter_system/validate_mock_data.py
```

测试覆盖：

- 五点检测、原图坐标恢复、置信度范围、8 位/16 位输入和非法配置；
- 测量点输入顺序变化后的稳定配对；
- 平移、旋转、等比例缩放、各向异性仿射形变、亮度和噪声；
- 三点、六点、粘连和角色歧义；
- 缺失光斑由伪点补足、逐点/中心残差和配对后置信度失败；
- 输入 JSON 不存在/损坏/含非标准注释、字段缺失、上游错误、绝对路径和父目录穿越；
- 统一配置缺段、额外字段、错误单位/路径策略、非标准注释或算法参数无效；
- 配置缺失、图片缺失/无法解码、输入输出路径重叠、并发写者和输出写入失败；
- 旧成功结果失效和双错误输出；
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

## 当前限制与 TODO_CONFIRM

1. 真实标定图、测量图和专用配置尚未提供，只有合成数据完成成功路径验证。
2. M1 补充 `input_package.json` 引用的 TIFF 和配置文件未随样例提供，目前只能验证 `CONFIG_NOT_FOUND` 错误链路。
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
13. 两份 spots 是两个独立文件，无法在普通文件系统上完成一次跨文件原子重命名；正常返回路径会回滚第二次发布失败，但进程被强制终止时仍可能留下半组文件。调用方必须以 CLI 退出成功和最后发布的运行日志为完成边界。
14. `TODO_CONFIRM`：负责人已选择方案 C，并要求身份不确定时报错；硬件侧暂时无法确认真实相对旋转范围。当前实现能拒绝直接超过 `35` 度的候选和已检测到的多解，但无法识别完全对称十字的 90 度隐藏别名；仓库外 `+60` 度探针当前会错误成功。负责人审查后仍需决定采用非对称标记、稳定外观特征还是硬件身份锚点，不能仅调整角度阈值后宣称问题已解决。
