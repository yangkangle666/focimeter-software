# 自动焦度计软件项目

本仓库用于开发自动焦度计的软件部分：读取标定图与测量图，识别光斑，计算镜片参数，并在本地系统中展示结果。

## 开发分支

| 分支 | 负责板块 |
|---|---|
| `main` | 稳定版本 |
| `develop` | 四个板块合并与联调 |
| `feature/m1-input` | 输入与配置模块 |
| `feature/m2-image` | 图像识别模块 |
| `feature/m3-calc` | 标定与计算模块 |
| `feature/m4-system` | 本地系统与展示模块 |

成员开始开发前，应先切换到自己负责的分支，不得直接在 `main` 或其他组分支开发。

## 仓库结构

```text
focimeter_system/          四模块正式开发区
docs/                      项目管理、答辩和汇报材料
references/                标准、论文和原始项目资料
reference_implementation/  原 C++ 答辩依据工程
```

## 开工前必读

1. `focimeter_system/docs/interface_contract_v1.md`
2. `focimeter_system/config/default_config.json`
3. 自己模块目录内的 `README.md`

统一接口和统一配置是单一来源。任何人和 AI 都不得在模块内复制后私自修改。

## 数据链

```text
M1 input_package.json
  -> M2 spots_calib.json + spots_meas.json
  -> M3 result.json
  -> M4 界面、日志和报告
```
