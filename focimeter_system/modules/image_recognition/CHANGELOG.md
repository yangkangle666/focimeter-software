# M2 更新日志

## 0.4.0 - 2026-08-04

### 新增

- 新增 `1600 x 1200`、8-bit RGB 绿色光斑 JPEG 的实验检测适配；亮度图负责分割，绿色通道负责质心加权和强度诊断。
- 新增 `m2_jpeg_detection_and_determinism` 测试，覆盖 JPEG 两次编码、94 点保持、暗心/过曝核心、尘点、小碎片、完整近边点、边缘亮点串扰、不等面积近邻短暗缝、连续低亮度光晕下的独立双峰、斜向粘连、候选安全上限和三次重复序列化确定性。
- 新增两组仓库真实 JPEG 输入包的 CLI 软件回归，固定验证触边候选非致命、实验接口边界和镜片一近圆大连通域不被误报为粘连；27 点仅作为当前软件回归基线，不是物理真值。
- 邻近碎片抑制改为保守的双证据规则：峰间亮度不得出现深谷，或候选距离必须显著小于图内典型点间距；只有面积同时明显更小时才剔除。
- 实验输出新增 `bounding_box_elongation_ratio`、`principal_axis_elongation_ratio`、`rejected_shape_count`、`rejected_proximity_count`、`segmentation_source` 和 `centroid_intensity_source` 诊断字段。
- 新增 [REAL_JPEG_SOFTWARE_VALIDATION.md](REAL_JPEG_SOFTWARE_VALIDATION.md) 和不含原图的 M3 联调 JSON 样例 `samples/real_jpeg_software_verified/`。

### 修复与强化

- 触边候选由“使整图失败”改为单独剔除并报告 warning；候选靠近边缘时检查原始绿色信号是否实际延伸到图像边界，避免把留有暗背景的完整近边点误删。
- 增加基于当前图中位面积的小候选剔除和低矩形填充率形状剔除，减少 JPEG 压缩碎片、尘点和异常连通域进入结果。
- “疑似粘连”改为同时要求面积偏大和二阶矩主轴伸长，既避免把面积较大但近圆的真实光斑误判为粘连，也覆盖包围框接近正方形的斜向粘连。
- `detection_id` 排序使用精确 `(y, x, component_label)` 次序，消除容差比较器不满足严格弱序的风险。
- 置信度信号项结合绿色通道局部残差与原始峰值；它仍是未统计标定的工程评分，不是概率。
- 彩色输入的绿色通道信号明显弱于 BT.601 亮度时，质心权重自动回退为亮度并报告 `GREEN_CHANNEL_SIGNAL_WEAK`，避免非绿色 RGB PNG/TIFF 产生空结果。
- 邻近碎片抑制要求较小面积，并使用峰间 50% 深谷或图内中位最近邻间距 20% 的子间距证据；子间距规则在没有亮桥时额外报告 `SUBPITCH_FRAGMENT_REJECTED_UNVERIFIED`，明确该规则尚无真实逐点身份真值。其他证据不足时保留并报告 `NEARBY_CANDIDATE_UNRESOLVED`。
- 候选安全上限前移到两两邻近检查之前，避免高噪声图在返回数量错误前进入无界的二次复杂度扫描。

### 接口

- `schema_version=m2.multispot.experimental.1`、`matching.status=not_performed`、`id_scope=image_local`、`physical_identity_guaranteed=false` 和 `metrology_validated=false` 保持不变。
- 不新增 `spot_id`，不执行跨图匹配，不修改统一 v1 契约、默认配置或 M1/M3/M4。

### 验证状态

- Visual Studio 2022/CMake 的 Debug 与 Release 构建均通过，两个配置各 `16/16` 项 CTest 通过；仓库统一 mock 校验 `42/42` 个 JSON 通过，并核验 3 个 mock 输入包路径。
- 外置真实 JPEG：候选参考图和两张候选测量图均输出 27 点，三张图均为 `quality.is_usable=true`。
- 参考图原始候选 `57`，剔除触边 `14`、面积异常 `15`、邻近碎片 `1`；测量图 1 为 `37/0/7/3`，测量图 2 为 `52/17/7/1`；三张图形状剔除均为 `0`。
- 两组真实输入对各连续运行三次，覆盖当前参考图和两张测量图；每个输入对应的 JSON 均只有一个 SHA-256，坐标、`detection_id` 与质量字段保持确定性。
- 旧/新参考 JPEG 均检测 27 点；贪心最近邻比较的坐标均值偏差 `0.927 px`、RMS `1.027 px`、最大 `1.891 px`。图像 MAE 为 `0.559/255`，PSNR 为 `40.625 dB`。
- 验证状态始终为 `software_verified` / `software_only`、`metrology_validated=false`；JPEG 不是计量精度证据。

### 已知限制

- 本次只有一组来源相同的 JPEG，缺少相机原始格式、曝光/焦距、阵列参数、标准镜片证书和重复采集，不足以标定阈值或证明真实识别率。
- 参考图和两张测量图的有效点数为 `27/27/27`，M2 未判断这些点的跨图物理身份；点数相同不是配对证据，M3 必须独立匹配并保守拒绝缺点、伪点和对称歧义。
- 大面积近圆候选目前保留并标记 `AREA_ABOVE_MEDIAN`；它可能是合理的过曝光斑，也可能包含未分开的复杂结构，后续需要更多真实样本验证。

## 0.3.0 - 2026-07-27

### 新增

- 新增显式 `--experimental-multispot` 模式；默认五点 v1 路径、正式文件名、`spot_id`/`role` 和 `SpotMatcher` 保持不变。
- 新增独立 `MultispotDetector`：整图检测、中值滤波、顶帽局部背景残差、边缘背景估计、自动阈值、连通域、面积/边缘筛选和强度加权质心。
- 新增隔离的 `experimental_multispot/` JSON 和运行日志，使用图内 `detection_id`，明确 `experimental=true`、`contract_status=proposed`、`validation_scope=simulation_only`、`physical_identity_guaranteed=false` 和 `metrology_validated=false`。
- 新增点面积、原图峰值、残差峰值、残差积分、置信度、质量标记及候选筛选诊断。
- 新增 25 点、94 点、平移、局部形变、独立亮度变化、低对比度、梯度、噪声、缺失、过多、粘连、边缘、全暗和全亮合成样例，以及固定种子生成器与真值 manifest。
- 新增多光斑单元/模块/CLI 测试、公共失败链路测试和提交 fixture 对生成器的一致性校验。
- 新增 [MULTISPOT_INTERFACE_PROPOSAL.md](MULTISPOT_INTERFACE_PROPOSAL.md) 与 [LM700_MULTISPOT_MIGRATION_NOTES.md](LM700_MULTISPOT_MIGRATION_NOTES.md)。

### 修复与强化

- 置信度信号分量统一使用顶帽残差域，避免用原图峰值减增强图阈值造成量纲混用。
- 实验成功 JSON 二次校验点数范围；`quality.is_usable` 根据全部候选的最低可用置信度计算，不再固定为 `true`。
- 实验错误 JSON 拒绝空错误对象；输出字段将残差积分明确命名为 `integrated_residual_8bit_pixel`。
- 16 位实验输入必须显式提供白电平，支持例如 12 位数据装在 16 位容器的稳定缩放，并拒绝码值超过白电平的配置。
- `data_source` 只在实验模式解析，不改变默认五点 v1 输入的接受行为。
- 修复实验模式真实锁文件位于输出根目录、但别名保护错误检查子目录的问题。
- 标定图与测量图像素尺寸不一致时，在检测错误之前统一返回 `COORDINATE_SYSTEM_INVALID`。
- CTest 输出按构建目录和配置隔离，Windows 运行时 DLL 部署锁等待时间提高到 300 秒。

### 接口

- `interface_contract_v1.md`、`default_config.json`、M1/M3/M4 和正式五点输出无修改。
- 多光斑输出是 M2 私有实验提案，不使用正式 `spot_id` 或五点 `role`，匹配所有者标为 `owner_status=unassigned`。
- `--experimental-16bit-white-level` 是实验 CLI 参数，不是已批准的统一硬件配置字段。

### 验证状态

- 全新 Visual Studio 2022/CMake Debug 构建通过，Debug CTest `13/13` 通过。
- 全新 Visual Studio 2022/CMake Release 构建通过，Release CTest `13/13` 通过。
- 仓库统一 mock 校验 `36/36` 个 JSON 通过；多光斑生成器两次输出一致，且与提交的全部生成文件 SHA-256 一致。
- 显式五点 CLI 成功样例输出 `5+5` 个 spots；缺失点样例退出码为 `3`，测量输出为 `SPOT_COUNT_MISMATCH`。
- 显式 94 点多光斑 CLI 成功样例输出 `94+94` 个 detections，`physical_identity_guaranteed=false`；粘连样例退出码为 `3`，测量输出为 `CENTROID_FAILED`。
- M3 当前分支 validator 源码已只读提取，但本机现有 Python 环境缺少 `jsonschema`，依赖策略禁止自动安装，因此本阶段未重新运行；PR #4 已记录的五点 validator 结果不等同于本次新执行。

### 已知限制

- 本阶段仅为 `software_verified` / `simulation_only`；没有 LM700 实拍图、Hartmann 标定表、相机标定或标准镜片计量结果。
- 当前连通域检测没有实现老师网格原型中的局部极大值与最小间距去重，真实光斑碎裂、紧密多峰和不同间距仍需专项对比。
- 多光斑参数仍是 M2 内部实验默认值，真实数据到位后需要公共配置提案。
- 跨图多光斑匹配、位移场、波前和 S/C/A 均未实现，且匹配所有者尚未获负责人批准。
- 五点完全对称十字的 90 度隐藏身份别名是 PR #4 已知限制，本阶段未改变也未声称解决。
- 失败路径会先发布成对错误 JSON 再写运行日志；极端磁盘写入故障可能留下错误 JSON 已发布但日志缺失或不完整，调用方仍应以进程退出码和完整日志共同判断任务完成。

## 0.2.0 - 2026-07-25

### 新增

- 新增结构化图像诊断，记录尺寸、通道、源位深、归一化 8 位 ROI 灰度统计、过滤后候选数和启发式 warning。
- 新增欠曝、过曝、低对比度和疑似完整 Hartmann 阵列提示；识别失败时把可获得的诊断附加到既有错误 `details`，未扩展统一错误码。
- `--save-intermediate` 新增 `calibration_diagnostics.json` 与 `measurement_diagnostics.json`，成功及图像识别失败都会保留可获得的中间阶段；运行日志明确标记 `software_verified` 和 `metrology_validated=false`。
- 合成数据新增低对比度、高斯模糊、背景梯度、适度光斑差异、全暗、全亮、25 点阵列和 ROI 边界案例。
- 新增 [REAL_DATA_REQUIREMENTS.md](REAL_DATA_REQUIREMENTS.md) 和 [INTEGRATION_NOTES_M2_M3_M4.md](INTEGRATION_NOTES_M2_M3_M4.md)，分别说明真实数据验收条件与模块联调边界。

### 修复与强化

- 调整曝光提示条件，避免把正常的暗背景五光斑图误报为整图欠曝。
- 标定图与测量图像素尺寸不一致时，在匹配前返回 `COORDINATE_SYSTEM_INVALID`，避免比较不在同一像素坐标空间中的点。
- 配置已声明相机宽高时校验解码尺寸，并在语义无效输入包的错误清理前保护已解析的输入路径别名。
- 合成图生成器继续使用固定随机种子，并由 manifest 明确区分 `synthetic_verified` 与真实计量验证。

### 接口

- `spots_calib.json`、`spots_meas.json` 和统一配置没有字段变化。
- 新增内容只位于 M2 私有运行日志、中间诊断产物、测试数据和 M2 文档。

### 验证状态

- 当前状态仅为 `software_verified` / `synthetic_verified`，不是 `metrology_validated`。
- Visual Studio 2022/CMake 的 Debug 与 Release 构建均通过，两个配置各 `8/8` 项 CTest 通过；关闭 CLI、测试和工具后的核心静态库 Release 独立构建通过。
- 仓库统一 mock 校验 `22/22` 个 JSON 通过；合成生成器产出的 `22/22` 个受管理文件与提交版本 SHA-256 一致。
- 最终 synthetic CLI 成功样例输出两组各五个 spots，M3 当前任务分支 validator 的 `contract` 与 `calculation-ready` 模式均通过，共 `2/2`。
- M1 补充 bundle 退出码为 `2`，双输出为 `CONFIG_INVALID`，诊断记录其配置声明 `12 x 12`、实际 TIFF 为 `1280 x 1024`；该结果只证明配置错误链路。
- M4 尚无可运行实现，本阶段只完成固定成功/失败材料和消费边界文档，未宣称四模块端到端运行通过。

### 已知限制

- 完全对称五点十字仍有 90 度周期的几何身份别名；真实物理 `spot_id` 需要非对称标记、稳定外观特征或硬件身份锚点。
- 图像质量阈值、顶帽核、检测和配对门限均未用真实设备图标定。
- 没有真实成对图片、相机/光学参数和标准镜片结果，不能评价真实识别率、物理配对正确率或计量精度。
- 当前 M1 补充 bundle 的配置声明 `12 x 12`，与两张 `1280 x 1024` TIFF 不一致；该输入只能验证 `CONFIG_INVALID` 错误链路。修正配置后，其 AI 阵列图仍不满足第一阶段五点合同。

## 0.1.0 - 2026-07-23

### 新增

- 将原 C++/OpenCV 半成品中的 ROI、灰度化、中值滤波、顶帽增强、二值化和质心思路迁移为 `focimeter_m2_core` 静态库。
- 新增非交互 CLI、CMake/Visual Studio 2022 构建、统一 JSON 输入输出、运行日志和可选中间图。
- 新增整图坐标恢复、面积/圆度/亮度筛选和 0 到 1 置信度。
- 新增标定图角色分配与测量图跨图一一配对；测量图不再独立排序后复用 ID。
- 新增保持方向的受约束仿射配对，支持柱镜导致的各向异性形变，并增加逐点与中心残差门限。
- 新增 16 位无符号图像归一化、非法位深/配置的结构化失败和模块边界异常转换。
- 新增输出目录跨进程锁、输入/输出路径重叠保护、成对 JSON 暂存发布、旧结果失效和失败时双错误输出。
- 输出目录锁拒绝同一锁对象重复获取，异常恢复只使用本次已读取且完成路径重叠检查的输入快照，避免恢复流程误清理输入文件。
- 新增完整统一配置结构校验，与 M3 当前配置 schema 对齐；标准 JSON 注释和未声明字段会被拒绝。
- 新增可重复合成图生成器、真值清单及成功/失败样例。
- 新增自包含 C++ 测试和 CLI 集成测试。
- 新增 Windows 宽字符 CLI 入口，可在中文项目路径下读取和输出文件。

### 修复的原实现问题

- 不再写死 `D:` 图片路径和 `F:` OpenCV 路径。
- 不再无条件弹窗和 `waitKey(0)`。
- 不再把 ROI 局部质心误当作整图坐标。
- 不再简单保留面积最大的五个轮廓。
- 不再让核心静态库依赖 OpenCV 窗口系统；`--show` 由 CLI 负责。
- 不再在方向分配冲突时把候选塞进空 ID。
- 在项目根目录探测前校验配置与图像的原始相对路径，非法绝对路径、父目录穿越和 UNC 路径不会先触发文件系统探测。
- 在非法路径错误输出前执行不访问文件系统的词法别名保护，防止输入声明指向受管输出时被清理或覆盖。
- M2 不再包含坐标焦度计算或 S/C/A 逻辑。

### 接口

- 未修改统一接口契约或默认配置。
- 成功输出对齐现有 M2 mock 和 M3 实际 schema。
- 任一图正常失败时两张 spots JSON 都返回错误；M3/M4 必须等待 CLI 完成后再消费结果。
- M2 自己生成的成功、错误和运行日志固定声明 `schema_version=1.0`，不继承不支持的上游版本。
- 负责人确认继续采用方案 C，身份不确定时必须报错，并允许先将当前实现推送到个人任务分支供代码审查；该授权不等于真实物理身份验收通过。

### 已验证

- 最终源码的 Debug 与 Release CMake/MSVC 构建均通过，两个配置各 `7/7` 项 CTest 通过；Windows 构建后自动部署实际依赖的 OpenCV 运行时及编解码 DLL。
- 关闭 CLI、测试和工具后，`focimeter_m2_core` 静态库可独立配置并完成 Release 构建，核心库不依赖 `opencv_highgui`。
- 仓库统一 mock 校验 `21/21` 个 JSON 通过；M3 任务分支的实际 validator 对三组合成输出分别执行 `contract` 和 `calculation-ready`，共 `6/6` 通过。
- 合成生成器重新生成 `14/14` 个受管理文件，SHA-256 与仓库版本一致。
- 五光斑识别、ROI 原图坐标、8/16 位输入、平移/旋转/等比例与各向异性缩放、检测顺序变化、亮度和轻度噪声。
- 三/六光斑、粘连、角色歧义、伪点替代、输入包缺失、图片缺失/无法解码、无效或带注释 JSON、无效配置、绝对路径、父目录穿越和输出失败。
- CLI 在当前中文项目路径下运行。
- 仓库 mock JPG 被正确拒绝为非五光斑图，CLI 退出码为 `3`，双输出返回 `SPOT_COUNT_MISMATCH`。
- M1 补充输入的 JSON 可解析，但它引用的配置和两张 TIFF 均不存在；CLI 退出码为 `2`，双输出返回 `CONFIG_NOT_FOUND`。该结果只证明错误链路，不代表真实图片联调成功。

### 已知限制

- 真实焦度计成对光斑图尚未提供，不能声明真实物理配对、识别精度或计量精度已验证。
- M1 补充输入缺少 `background.tif`、`R25M0004g.tif` 和任务配置，无法完成成功联调。
- 配对和置信度门限需要真实数据校准。
- `confidence` 是工程评分而非统计概率；输入文件/解码尺寸上限尚未设置。
- 两个独立 spots 文件不能跨文件原子提交；输出锁可防止并发写入，但调用方仍须以进程成功退出和最后发布的运行日志作为完成边界。
- 完全对称五点图形存在 90 度周期的几何身份别名；没有额外方向标记时不能宣称无条件物理光线识别。
- 负责人已确认硬件侧暂时无法给出相对旋转范围，并选择方案 C。仓库外 `+60` 度诊断探针会被当前实现解释为约 `-30` 度并错误成功，说明角度门限不能识别隐藏别名；本次提交是供负责人审查的实现，不是物理身份验收完成版。
- 项目根目录、`spot_id` 语义、完整 M2 错误码和双输出失败规则仍需负责人写入统一契约。

### 后续建议

1. 向 M1 获取 JSON 连同其引用的两张 TIFF 和配置文件，保持相对目录结构完整。
2. 采集多组同一任务的真实标定/测量图，人工标注五条物理光线作为真值。
3. 基于实拍曝光、噪声和粘连统计重新标定检测、置信度和配对门限。
4. 若实拍存在明显照明不均，以对照测试重新标定或替换当前区域自适应 Otsu 参数。
5. 由负责人统一补充接口契约，避免 M2 与 M3 私有 schema 漂移。
6. 若设备可能出现超过暂定旋转范围的姿态，增加非对称方向标记或由硬件提供身份锚点，再重新设计跨图配对。
