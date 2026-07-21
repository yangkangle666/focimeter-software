# Git/GitHub Collaboration Guides Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Create one novice-friendly Word handbook for team members and one mandatory Markdown operating standard for their coding AIs, both tied to this repository's Git/GitHub workflow.

**Architecture:** The AI Markdown file is the machine-readable operating policy and references the existing interface contract, configuration, and module README files as authoritative sources. The Word handbook explains the same branch lifecycle as a step-by-step SOP without duplicating interface schemas. A transient Python builder creates the DOCX with deterministic styles; render output is used only for visual QA and is removed before commit.

**Tech Stack:** Markdown, Git, GitHub CLI, bundled Python 3, `python-docx`, OOXML helpers, LibreOffice-based `render_docx.py`.

---

## File Map

- Create: `docs/governance/焦度计项目GitHub协作开发规范_AI必读版.md` - mandatory AI Git/GitHub behavior and fixed startup prompt.
- Create: `docs/governance/焦度计项目成员GitHub规范开发手册_人看版.docx` - human-facing operational handbook.
- Create temporarily, then delete: `.tmp/build_member_github_handbook.py` - deterministic DOCX builder.
- Create temporarily, then delete: `.tmp/docx_render/` - rendered QA pages and debug PDF.
- Do not modify: `focimeter_system/docs/interface_contract_v1.md`, `focimeter_system/config/default_config.json`, module `README.md` files, or existing governance deliverables.

### Task 1: Author the AI Git/GitHub operating standard

**Files:**
- Create: `docs/governance/焦度计项目GitHub协作开发规范_AI必读版.md`
- Reference: `docs/superpowers/specs/2026-07-21-github-collaboration-docs-design.md`
- Reference: `docs/governance/焦度计项目AI开发总规范_AI看版.md`
- Reference: `focimeter_system/docs/interface_contract_v1.md`
- Reference: `focimeter_system/config/default_config.json`

- [ ] **Step 1: Write the policy with fixed authority and scope rules**

Use these top-level sections in this order:

```text
适用方式
AI 开工前固定提问
权威文件优先级
模块、目录与长期分支映射
开工前 Git 检查
标准任务分支流程
允许和禁止修改的范围
AI 实现与验证要求
提交、推送与 Pull Request 规则
冲突、脏工作区和异常处理
完成时固定报告格式
可直接粘贴给 AI 的启动模板
```

State the exact long-lived branch mapping and directory mapping from the design. Require personal branches named `task/<module>-<member>-<short-description>`. Require PR targets to be the matching `feature/...` branch. State that only the project lead merges `feature/... -> develop -> main`.

- [ ] **Step 2: Encode safe Git behavior**

Require these checks before edits:

```powershell
git status -sb
git branch --show-current
git fetch origin
git pull --ff-only origin <本组feature分支>
```

Forbid `git reset --hard`, `git clean -fd`, force push, history rewriting, blanket deletion, committing secrets, and silently overwriting user changes. Require the AI to stop and report when the worktree is dirty with unrelated changes or when the current branch does not match the assigned module.

- [ ] **Step 3: Encode implementation and delivery checks**

Require reading the interface contract, unified config, module README, and mock-data README before implementation. Require relevant runnable checks, `python focimeter_system/validate_mock_data.py`, `git diff --check`, and a final `git status --short`. Require staging explicit paths and a Conventional Commit message such as:

```powershell
git add -- focimeter_system/modules/input_config tests
git commit -m "feat(m1): add input package validation"
git push -u origin task/m1-zhangsan-input-validation
```

- [ ] **Step 4: Validate Markdown structure and terminology**

Run:

```powershell
rg -n "main|develop|feature/m1-input|feature/m2-image|feature/m3-calc|feature/m4-system|task/|Pull Request|interface_contract_v1.md|default_config.json" docs/governance/焦度计项目GitHub协作开发规范_AI必读版.md
rg -n "T[B]D|T[O]DO|待[补]充|以后[再]写|密[码]|Token 示例" docs/governance/焦度计项目GitHub协作开发规范_AI必读版.md
git diff --check
```

Expected: all required terms are present; the placeholder/sensitive-example scan has no output; `git diff --check` exits successfully.

### Task 2: Create the novice-facing Word handbook

**Files:**
- Create temporarily: `.tmp/build_member_github_handbook.py`
- Create: `docs/governance/焦度计项目成员GitHub规范开发手册_人看版.docx`
- Reference: `docs/governance/焦度计项目GitHub协作开发规范_AI必读版.md`

- [ ] **Step 1: Build a deterministic document generator**

Use bundled Python:

```powershell
$python = 'C:\Users\yangkangle\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe'
& $python .tmp\build_member_github_handbook.py
```

Implement these builder units:

```python
configure_page_and_styles(document)
configure_numbering(document)
add_editorial_cover(document)
add_quick_start(document)
add_repository_and_branch_map(document)
add_first_time_setup(document)
add_standard_task_workflow(document)
add_ai_handoff_workflow(document)
add_review_and_merge_workflow(document)
add_prohibited_actions(document)
add_troubleshooting(document)
add_checklists(document)
audit_document_structure(document)
```

Resolve `compact_reference_guide` as follows:

```text
Page: US Letter portrait; all margins 1.0 in; header/footer 0.492 in.
Body: Calibri 11 pt; East Asia font Microsoft YaHei; 6 pt after; 1.25 line spacing.
H1: 16 pt #2E74B5; 18 pt before; 10 pt after.
H2: 13 pt #2E74B5; 14 pt before; 7 pt after.
H3: 12 pt #1F4D78; 10 pt before; 5 pt after.
Lists: marker 0.187 in; text 0.375 in; hanging 0.188 in; 4 pt after; 1.25 line spacing.
Tables: total 9360 DXA; indent 120 DXA; cell margins top/bottom 80 and start/end 120 DXA; #E8EEF5 header fill.
Named CJK override: Microsoft YaHei for w:eastAsia on all Chinese text.
Named manual-title override: restrained editorial cover, 28 pt #0B2545, no decorative objects.
Header: 自动焦度计软件项目 | 团队协作手册.
Footer: page-number field, muted and right aligned.
```

Use real Word heading styles, real list numbering, and fixed DXA table geometry. Do not use fake bullet characters, manual numbered paragraphs, percentage-width tables, fixed row heights, or text boxes.

- [ ] **Step 2: Include complete member actions**

The handbook must contain:

```text
Member information template: name, GitHub username, profile URL, module, partner.
Six long-lived branches and four module directory mappings.
Accept-invitation and clone instructions.
Git identity configuration commands with non-personal placeholders.
Per-task commands for pull, task branch creation, status check, explicit staging, commit, push, and PR.
Partner review checklist and project-lead merge sequence.
Instruction to make AI read the Markdown policy before every task.
Troubleshooting for wrong branch, dirty worktree, rejected push, merge conflict, accidental shared-file edit, and AI scope overrun.
Start-of-work, pre-commit, and pre-merge checklists.
```

- [ ] **Step 3: Generate and structurally audit the DOCX**

Run the builder and then inspect the resulting package with bundled Python and `python-docx`. Expected checks:

```text
File exists and is non-empty.
At least 8 Heading 1 sections.
All four feature branch names occur in extracted paragraph/table text.
The AI Markdown filename occurs in the handbook.
No unfinished placeholder marker, personal absolute path, password, real token, or fake GitHub account occurs.
All tables have explicit fixed widths and repeat header rows where appropriate.
```

### Task 3: Render and visually inspect the Word handbook

**Files:**
- Verify: `docs/governance/焦度计项目成员GitHub规范开发手册_人看版.docx`
- Create temporarily: `.tmp/docx_render/page-*.png`

- [ ] **Step 1: Render every page**

Run:

```powershell
$python = 'C:\Users\yangkangle\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe'
$renderer = 'C:\Users\yangkangle\.codex\plugins\cache\openai-primary-runtime\documents\26.715.12143\skills\documents\render_docx.py'
& $python $renderer 'docs\governance\焦度计项目成员GitHub规范开发手册_人看版.docx' --output_dir '.tmp\docx_render' --emit_pdf
```

Expected: one PNG per page and a non-empty debug PDF.

- [ ] **Step 2: Inspect every PNG at original resolution**

Check every page for missing Chinese glyphs, clipped text, code lines outside margins, overlapping objects, broken tables, cramped cells, isolated headings, large blank gaps, and inconsistent headers/footers.

- [ ] **Step 3: Fix and repeat the render loop**

If any defect exists, modify `.tmp/build_member_github_handbook.py`, rebuild the DOCX, clear only `.tmp/docx_render`, rerender, and reinspect every page. Repeat until all pages pass.

### Task 4: Cross-check and commit the two deliverables

**Files:**
- Add: `docs/governance/焦度计项目GitHub协作开发规范_AI必读版.md`
- Add: `docs/governance/焦度计项目成员GitHub规范开发手册_人看版.docx`
- Delete before staging: `.tmp/build_member_github_handbook.py`
- Delete before staging: `.tmp/docx_render/`

- [ ] **Step 1: Run repository checks**

```powershell
python focimeter_system/validate_mock_data.py
git diff --check
git status --short
```

Expected: 16 JSON files validate, no whitespace errors, and only the two deliverables plus this already-committed plan are in scope.

- [ ] **Step 2: Verify cross-document consistency**

Extract DOCX text with `python-docx` and compare required branch names, module names, merge direction, prohibited branches, source-of-truth paths, and AI policy filename against the Markdown file. Expected: zero missing or contradictory required terms.

- [ ] **Step 3: Commit only the deliverables**

```powershell
git add -- 'docs/governance/焦度计项目GitHub协作开发规范_AI必读版.md' 'docs/governance/焦度计项目成员GitHub规范开发手册_人看版.docx'
git diff --staged --check
git commit -m "docs(governance): add GitHub collaboration guides"
```

### Task 5: Integrate and publish the common documentation

**Files:**
- Git refs only; no new content files.

- [ ] **Step 1: Fetch and verify divergence before merging**

```powershell
git fetch origin --prune
git status -sb
git log --oneline --decorate --all -12
```

Stop if any target branch has remote commits not previously reviewed. Never force push or overwrite member work.

- [ ] **Step 2: Push the task branch and merge to develop**

```powershell
git push -u origin task/governance-github-docs
git switch develop
git pull --ff-only origin develop
git merge --ff-only task/governance-github-docs
git push origin develop
```

- [ ] **Step 3: Synchronize common docs to the stable and module branches**

For each target branch `main`, `feature/m1-input`, `feature/m2-image`, `feature/m3-calc`, and `feature/m4-system`, switch to the branch, fast-forward from its remote, merge `develop` using `--ff-only` while histories still permit it, and push. If fast-forward is not possible, stop and use a reviewed normal merge instead of force.

- [ ] **Step 4: Verify GitHub state**

Run:

```powershell
git ls-remote --heads origin
gh repo view yangkangle666/focimeter-software --json url,visibility,defaultBranchRef
git status --porcelain=v1 -b
```

Expected: the task branch and all six long-lived branches exist remotely; `main` remains the private repository's default branch; target branches contain the two deliverables; local worktree is clean.
