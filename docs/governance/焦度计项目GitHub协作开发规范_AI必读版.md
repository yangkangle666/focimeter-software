# 焦度计项目 GitHub 协作开发规范（AI 必读版）

> **历史分支说明（2026-08-07）**：本文中的长期 `feature/m1-input`、`feature/m2-image`、`feature/m3-calc`、`feature/m4-system` 流程属于早期方案。当前新任务统一从最新 `origin/develop` 创建新的 `task/m1-*`、`task/m2-*`、`task/m3-*` 或 `task/m4-*` 分支，并通过 PR 合入 `develop`；除非负责人明确指定，不得执行本文旧 `feature/*` 命令。当前状态和第一步/第二步安排见仓库根 `README.md`。

> 本文件用于约束参与本项目开发的编程 AI。每次开始新任务时，成员必须先让 AI 完整阅读本文件。AI 必须确认成员负责的模块、当前阶段、任务范围和 GitHub 交付终点，再进行任何文件修改。

## 1. 适用方式

本项目由 8 名成员组成，每两人负责一个模块。团队主要依靠 AI 辅助编程，但代码责任仍由成员本人和同组审核者承担。

AI 在本项目中必须同时遵守以下原则：

1. 四个模块独立开发，通过统一 JSON 接口连接。
2. Git 分支决定修改归属，目录边界决定允许修改的文件。
3. AI 不得因为“实现更方便”而改变统一接口、配置、单位或坐标系。
4. AI 不得覆盖成员已有改动，不得使用破坏性 Git 命令。
5. AI 必须完成与改动相匹配的验证，不能只生成代码后宣称完成。
6. 未经成员明确授权，不得执行提交、推送、创建 Pull Request 或合并等外部写操作。

## 2. AI 开工前固定提问

AI 在读取或修改代码前，必须逐项取得明确答案。不能从聊天上下文中猜测缺失信息。

```text
请先确认本次开发信息：

1. 你的姓名或组内称呼是什么？
2. 你负责哪个板块？
   - M1 输入与配置模块
   - M2 图像识别模块
   - M3 标定与计算模块
   - M4 本地系统与展示模块
3. 当前处于哪个阶段？
   - 第一阶段：四个模块独立成品
   - 第二阶段：四模块合并与修复 bug
   - 第三阶段：优化创新与软件化
4. 本次具体任务和验收标准是什么？
5. 当前仓库路径和现有输入材料在哪里？
6. 本次希望 AI 工作到哪一步？
   - 只在本地修改并验证
   - 修改、验证并提交 commit
   - 修改、验证、推送并创建 Pull Request
7. 是否有项目负责人批准的共享文件或接口变更？如果有，请提供批准内容。
```

出现以下任一情况时，AI 必须继续询问，不能开始修改：

- 成员没有说明负责哪个模块。
- 任务同时跨越两个以上模块，但没有项目负责人批准。
- 要求修改统一接口或统一配置，但没有项目负责人批准。
- 验收标准只有“做完”“能运行”等无法验证的描述。
- 成员要求推送或合并，但没有说明目标分支。

## 3. 权威文件优先级

AI 必须先读取权威文件，再判断任务能否执行。不得用聊天中临时生成的字段覆盖仓库内规范。

优先级从高到低如下：

1. 项目负责人明确批准的任务或变更决定。
2. `focimeter_system/docs/interface_contract_v1.md`：接口唯一权威来源。
3. `focimeter_system/config/default_config.json`：配置唯一权威来源。
4. 本模块目录中的 `README.md`：模块职责、输入输出和运行方式。
5. `docs/governance/焦度计项目AI开发总规范_AI看版.md`：项目统一开发要求。
6. `docs/governance/四大板块开发注意事项_AI必读版.md`：各模块注意事项。
7. 成员对本次任务的说明。

发生冲突时，AI 必须指出具体冲突、引用文件路径并停止相关修改，等待项目负责人决定。

算法资料使用规则：

- `reference_implementation/focimeter_cpp/` 是现阶段算法答辩和实现分析的重要依据。
- 陈文婷文章可以用于理解项目原理和解释“为什么这样做”。
- 陈文婷文章中的计算过程不能作为本项目实现计算公式的依据。
- 不确定的真实硬件参数必须保留为待确认状态，不能由 AI 编造。

## 4. 模块、目录与长期分支映射

| 模块 | 长期分支 | 默认允许修改的模块目录 | 组内 PR 目标 |
| --- | --- | --- | --- |
| M1 输入与配置模块 | `feature/m1-input` | `focimeter_system/modules/input_config/` | `feature/m1-input` |
| M2 图像识别模块 | `feature/m2-image` | `focimeter_system/modules/image_recognition/` | `feature/m2-image` |
| M3 标定与计算模块 | `feature/m3-calc` | `focimeter_system/modules/calibration_calculation/` | `feature/m3-calc` |
| M4 本地系统与展示模块 | `feature/m4-system` | `focimeter_system/modules/local_system/` | `feature/m4-system` |

其他长期分支：

- `main`：稳定版本，只接收已经完成联调和验收的内容。
- `develop`：四模块集成与联调分支，只由项目负责人组织合并。

`main`、`develop` 和四个 `feature/...` 都是长期分支。成员日常开发必须使用个人短期任务分支，命名格式固定为：

```text
task/<模块编号>-<成员称呼>-<简短任务名>
```

示例：

```text
task/m1-member-a-input-validation
task/m2-member-b-centroid-detection
task/m3-member-c-result-quality
task/m4-member-d-report-export
```

分支名使用小写英文字母、数字和短横线，不使用空格、中文或个人敏感信息。

## 5. 开工前 Git 检查

### 5.1 确认仓库和远程地址

```powershell
git rev-parse --show-toplevel
git remote -v
git status -sb
git branch --show-current
```

AI 必须确认：

- 当前目录属于 `focimeter-software` 仓库。
- 远程仓库是项目负责人指定的 `origin`。
- 当前工作区是否存在未提交文件。
- 当前分支是否属于本模块或本次任务。

### 5.2 获取远程最新状态

```powershell
git fetch origin --prune
```

`git fetch` 只更新远程状态，不会自动改变工作区。获取完成后，AI 必须再次查看：

```powershell
git status -sb
git log --oneline --decorate -8
```

### 5.3 处理非干净工作区

如果 `git status --short` 有输出，AI 必须先判断这些改动是谁产生的：

- 本次任务相关且成员确认继续：可以保留并纳入本次工作。
- 与本次任务无关：不得暂存、修改、删除或覆盖。
- 与本次任务修改同一文件：停止操作，说明冲突文件并询问成员。
- 来源不明：视为成员已有工作，必须保留。

未经成员明确同意，AI 不得自动执行 `git stash`。

## 6. 标准任务分支流程

下面以 M1 为例。其他模块必须替换为映射表中的对应分支。

### 6.1 更新本组长期分支

```powershell
git switch feature/m1-input
git pull --ff-only origin feature/m1-input
```

如果 `--ff-only` 失败，说明本地和远程历史已经分叉。AI 必须停止并报告，不得自动执行 rebase、强制合并或重写历史。

### 6.2 创建个人任务分支

```powershell
git switch -c task/m1-member-a-input-validation
git status -sb
```

创建后必须确认当前分支是个人 `task/...` 分支。若同名分支已经存在，先检查本地和远程状态，不能直接删除重建。

### 6.3 任务完成后的流向

```text
个人 task 分支
  -> Pull Request 到本组 feature 分支
  -> 同组搭档审核
  -> 本组 feature 分支形成模块成品
  -> 项目负责人合并到 develop
  -> 四模块联调与修复
  -> 项目负责人合并到 main
```

AI 不得把个人任务分支直接合并到 `develop` 或 `main`。

## 7. 允许和禁止修改的范围

### 7.1 默认允许

- 本模块目录中的实现代码。
- 本模块目录中的测试代码和运行脚本。
- 本模块自己的 `README.md`，但只更新已经实现并验证过的运行方式。
- 与本模块对应的 mock 子目录中新增的测试样例，前提是不改变现有统一样例含义。

对应 mock 子目录：

| 模块 | 可按任务新增样例的目录 |
| --- | --- |
| M1 | `focimeter_system/data/mock/m1_input_config/` |
| M2 | `focimeter_system/data/mock/m2_image_recognition/` |
| M3 | `focimeter_system/data/mock/m3_calibration_calculation/` |
| M4 | `focimeter_system/data/mock/m4_local_system/` |

### 7.2 读取但不得擅自修改

- `focimeter_system/docs/interface_contract_v1.md`
- `focimeter_system/config/default_config.json`
- 其他模块目录
- `docs/defense/` 和现有 `docs/governance/` 文件
- `references/` 和 `reference_implementation/`
- 根目录 `.gitignore`、`README.md` 和仓库级工具

共享文件确需修改时，AI 必须先展示拟修改内容、影响模块和兼容方案，取得项目负责人明确批准后，使用单独任务分支处理。

### 7.3 绝对禁止

- 直接在 `main`、`develop` 或本组 `feature/...` 长期分支上开发日常功能。
- 修改其他组代码来绕过本模块问题。
- 私自更改 JSON 字段名、层级、状态值、错误格式或 `schema_version`。
- 私自更改单位、坐标系、光斑角色定义或 S/C/A 输出含义。
- 把个人电脑绝对路径写入正式代码或提交到仓库。
- 把真实密码、Token、私钥、账号凭据或含敏感信息的 `.env` 提交到仓库。
- 把缓存、虚拟环境、构建结果、临时渲染文件或大批无关生成物提交到仓库。
- 为了让测试通过而删除失败测试、降低断言或伪造算法结果。

## 8. AI 实现与验证要求

### 8.1 实现前

AI 必须说明：

1. 当前模块和任务分支。
2. 本次允许修改的文件范围。
3. 本次输入、输出和依赖的 mock 数据。
4. 计划执行的验证命令。
5. 是否需要项目负责人批准共享文件变更。

### 8.2 实现中

- 保持改动只解决当前任务，不进行无关重构。
- 使用仓库已有技术、目录和命名方式。
- 输入输出必须符合接口契约，不得只在控制台打印结果。
- 缺少真实硬件数据时使用统一 mock 数据，并明确标注模拟范围。
- 错误必须返回统一错误对象，不能静默失败或直接崩溃。
- 测试应覆盖成功输入、错误输入和本次修复的问题。
- 发现资料与代码不一致时记录证据，不擅自选择方便的一方。

### 8.3 实现后

至少执行：

```powershell
python focimeter_system/validate_mock_data.py
git diff --check
git status --short
git diff --stat
```

同时执行本模块 README 或测试目录规定的测试命令。没有可运行测试时，AI 必须明确说明缺口，不能声称“测试通过”。

AI 在交付前必须人工式复核以下内容：

- 修改文件是否全部属于任务范围。
- 是否意外改动统一接口、配置或其他模块。
- 输入输出 JSON 是否包含接口契约要求的公共字段。
- 路径是否为项目相对路径或配置路径。
- 是否包含个人信息、密钥、缓存或临时文件。
- 错误场景是否可复现并返回明确原因。

## 9. 提交、推送与 Pull Request 规则

只有成员在开工前或完成后明确授权，AI 才能执行本节操作。

### 9.1 提交前检查

```powershell
git status --short
git diff
```

AI 必须逐个确认待提交文件。禁止在混有无关改动时直接使用 `git add -A` 或 `git add .`。

正确示例：

```powershell
git add -- focimeter_system/modules/input_config/
git add -- focimeter_system/data/mock/m1_input_config/new_case.json
git diff --staged --check
git diff --staged --stat
```

### 9.2 提交信息

统一使用：

```text
<类型>(<模块>): <简短说明>
```

常用类型：

- `feat`：新增功能。
- `fix`：修复问题。
- `test`：新增或修改测试。
- `docs`：只修改文档。
- `refactor`：不改变功能的内部整理。
- `chore`：维护性工作。

示例：

```powershell
git commit -m "feat(m1): add input package validation"
git commit -m "fix(m2): reject incomplete spot sets"
git commit -m "test(m3): cover invalid coordinate input"
```

一次提交只包含一个逻辑任务。提交失败时修复原因后创建正常提交，不得跳过检查钩子或擅自修改已有提交。

### 9.3 推送个人任务分支

```powershell
git push -u origin task/m1-member-a-input-validation
```

禁止使用 `--force` 或 `--force-with-lease`。推送前再次确认远程名称和当前分支。

### 9.4 创建 Pull Request

PR 的目标分支必须是本组 `feature/...`，不能是 `develop` 或 `main`。

PR 标题使用与提交相同的清晰格式。PR 说明至少包含：

```text
完成内容：
修改原因：
影响范围：
输入与输出：
验证命令和结果：
当前限制或待确认事项：
```

创建后由同组另一名成员审核。作者不能代替搭档完成审核，也不能在未解决审核意见时自行宣布模块已验收。

## 10. 冲突、脏工作区和异常处理

### 10.1 切错分支但还没有修改

停止操作，切换到正确的本组 `feature/...`，更新后重新建立个人任务分支。

### 10.2 已经在错误分支产生修改

不要提交，不要删除修改，不要执行硬重置。报告：

```text
当前分支：
正确目标分支：
已修改文件：
是否存在提交：
建议迁移方式：
```

等待成员确认后再迁移。

### 10.3 推送被拒绝

先执行 `git fetch origin` 并比较本地与远程提交。不得用强制推送覆盖远程。说明远程新增提交和建议处理方式，等待成员决定。

### 10.4 发生合并冲突

AI 必须列出冲突文件，分别解释本地和远程修改意图。只有能证明合并结果同时满足接口和任务要求时才解决冲突；不确定时交给双方成员共同确认。

### 10.5 AI 修改越界

立即停止继续编辑，列出越界文件和具体差异。未经成员确认，不得自行删除或回滚，因为文件中可能还包含成员已有工作。

## 11. 禁止使用的 Git 操作

除非项目负责人针对明确文件和目的书面授权，AI 不得执行：

```text
git reset --hard
git clean -fd
git checkout -- <文件>
git restore <文件>
git push --force
git push --force-with-lease
git rebase -i
删除包含他人工作的分支
改写已经推送的提交历史
```

AI 不得为了“恢复干净状态”而清除无法确认来源的改动。

## 12. 完成时固定报告格式

AI 完成任务时必须按以下格式向成员报告：

```text
任务：
模块：
当前分支：

已完成：
- （填写）

修改文件：
- （填写）

输入与输出：
- 输入：
- 输出：

验证：
- 命令：
- 结果：

Git/GitHub 状态：
- 是否已提交：
- commit：
- 是否已推送：
- Pull Request：
- PR 目标分支：

未完成或待确认：
- （填写）
```

不得只回复“已完成”“可以运行”或“测试正常”。每项结论必须有文件、命令或 GitHub 状态作为证据。

## 13. 可直接粘贴给 AI 的启动模板

成员每次开始新任务时，将下面内容发送给 AI，并填写方括号中的信息：

```text
你正在参与自动焦度计软件项目开发。

请先完整阅读以下文件：
1. docs/governance/焦度计项目GitHub协作开发规范_AI必读版.md
2. docs/governance/焦度计项目AI开发总规范_AI看版.md
3. focimeter_system/docs/interface_contract_v1.md
4. focimeter_system/config/default_config.json
5. 我负责模块目录中的 README.md

本次信息：
- 成员称呼：[填写]
- 负责模块：[M1/M2/M3/M4 和完整模块名称]
- 当前阶段：[第一阶段/第二阶段/第三阶段]
- 本次任务：[填写]
- 验收标准：[填写]
- 输入材料：[填写文件路径；没有则说明使用哪个 mock]
- GitHub 交付终点：[仅本地修改/提交 commit/推送并创建 PR]
- 项目负责人批准的共享变更：[没有/填写批准内容]

开始前请先：
1. 检查 Git 仓库、远程地址、当前分支和工作区状态。
2. 说明本次允许修改和禁止修改的文件范围。
3. 说明将使用的输入、输出、测试和验证命令。
4. 发现分支错误、已有未提交改动、接口冲突或信息不足时停止并询问我。

未经项目负责人批准，不得修改统一接口、统一配置、其他模块或长期分支历史。
```

## 14. 最短执行清单

AI 每次任务至少完成以下闭环：

```text
[ ] 已确认成员、模块、阶段、任务和交付终点
[ ] 已读取接口契约、统一配置和模块 README
[ ] 已核对远程仓库、当前分支和工作区
[ ] 已从本组 feature 分支建立个人 task 分支
[ ] 修改范围没有越过本模块边界
[ ] 已运行模块测试和 mock 数据校验
[ ] 已检查差异、路径、接口、敏感信息和临时文件
[ ] 仅在获得授权后提交、推送和创建 PR
[ ] PR 目标是本组 feature 分支
[ ] 已按固定格式报告证据和待确认事项
```
