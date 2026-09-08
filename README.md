# WT_missile_atmosphere

> War Thunder 导弹运动模拟研究档案：**datamine 参数 + 引擎/社区物理源码 + 分析方法**

一个面向**导弹运动仿真算法研究**的资料仓库。起因是想搞清楚《战争雷霆》(War Thunder) 里导弹的空气动力学与运动建模到底用哪些量、怎么算。仓库把三类内容整理到一起：

1. **扒下来的（scraped）**：从公开 datamine 与开源引擎抓取到的原始数据/源码——载具参数 `.blkx`、Gaijin 开源物理代码、社区复现代码。
2. **解毒的（analyzed）**：对上面原始材料做的解析笔记——哪些 blk 字段对应哪个物理量、源码里每条公式的物理含义、与参数的对接表。
3. **引用的（referenced）**：调研过程依赖的上游仓库、官方 CDK 文档、气动方法论（DATCOM）等链接。

> ⚠️ 本项目与 Gaijin Entertainment 无任何隶属关系，仅用于教育与研究。War Thunder 客户端资源提取在 EULA 上属灰区，但 Gaijin 多年来对"只读 datamine"持默许态度；本项目仅二次分发**已公开**的数据与**开源**代码，不重新分发游戏客户端本身。

---

## 目录结构

```
WT_missile_atmosphere/
├── README.md
├── LICENSE                      # 本项目原创内容（两份笔记 + 本 README）以 MIT 发布
├── .gitignore
├── docs/
│   ├── 01_datamine_research_notes.md            # 解毒①：datamine 项目 / 导弹目录 / PL-15 参数清单
│   └── 02_physics_algorithm_source_excerpts.md  # 解毒②：物理算法源码摘录与对照（含 blk 参数对接表）
├── missile_samples/             # 扒下来：两个导弹 blk 样本（实为 JSON 文本，可直接打开）
│   ├── 9m39_igla_missile.blkx   #   9M39 针式肩射红外弹
│   └── cn_pl15_missile.blkx     #   PL-15 主动雷达中距弹
└── references/                  # 扒下来 + 引用：上游物理源码
    ├── gaijin_dagor/            #   Gaijin 开源引擎 DagorEngine（BSD-3-Clause）
    │   ├── LICENSE
    │   ├── atmosphere.cpp        #     大气密度/压力/温度/音速/粘度模型（4 阶多项式）
    │   ├── polares.cpp           #     飞机气动极曲线（DATCOM 分段：线性→抛物→失速→深失速）
    │   └── dynamicPhysModel.cpp  #     通用刚体 6-DOF + 碰撞（顺序冲量法）
    ├── community_wt_ballistics_calc/   # 社区 Rust 复现（Apache-2.0）
    │   ├── LICENSE
    │   ├── atmosphere.rs         #   大气模型复现（文件头明写"依 Gaijin atmosphere.cpp"）
    │   ├── runner.rs             #   导弹 1-DOF 质点弹道解算器（drag=0.5ρV²CxK·S + 两级推力）
    │   ├── launch_parameters.rs  #   发射参数结构体
    │   └── lib.rs
    └── community_wt_projectile/ # 社区 C++ 复现（上游未声明许可证，见下）
        ├── GameFuncs.cpp         #   炮弹阻力公式（弹道系数法 BC=-ρ·S·L/2m，半隐式稳定积分）
        ├── GameFuncs.h
        └── main.cpp
```

---

## 关键结论速览

详见 `docs/` 两份笔记。一句话总结：

- **大气、阻力、导弹质点弹道、刚体动力学骨架——WT 全部有可运行开源源码可抄**。
- **唯一要自己写的**：横向制导环 + 把 1-DOF 扩成 3/6-DOF；用 blk 的 `propNavMult=4` / `reqAccelMax` / `loadFactorMax` 当增益与限幅；转动惯量按圆柱几何自行估算。
- Dagor 引擎开源版**确实含物理代码**（`prog/gameLibs/gamePhys/`），但**没有导弹专用 6-DOF 飞行求解器**——那段在闭源游戏客户端，只通过 `.blkx` 暴露可调参数。

### blk → 物理量的核心映射（PL-15 为例）

| 仿真要算的 | blk 字段 | 值 |
|---|---|---|
| 参考面积 S | `caliber` | 0.203 m |
| 阻力 | `CxK`（叠 `dragCx`） | 1.6 |
| 一级/二级推力 | `propulsion0/1.impulse0.force` | 34500 / 17250 N |
| 工作时间 | `propulsion0/1.impulse0.time` | 3.0 / 2.5 s |
| 质量（总/级后） | `mass` / `mass_end` / `mass_end1` | 198 / 153 / 138 kg |
| 比例导引系数 | `guidanceAutopilot.propNavMult` | 4.0 |
| 可用过载上限 | `reqAccelMax` / `loadFactorMax` | 38 g |
| 抛射弹道 | `loftEnabled` / `loftElevation` | True / 20° |

---

## 复现取数（国内网络）

本机 `git clone` / `git push` 到 `github.com` 会被墙（Connection reset）。本项目所有上游源码均通过以下方式获取，便于复现：

- **目录树**：`https://api.github.com/repos/<owner>/<repo>/git/trees/<branch>?recursive=1`
- **单文件**：`https://cdn.jsdelivr.net/gh/<owner>/<repo>@<branch>/<path>`

默认分支需先查（`wt_ballistics_calc` 是 `master`，其余为 `main`）。

---

## 上游来源与许可证

| 内容 | 上游仓库 | 许可证 | 本项目位置 |
|---|---|---|---|
| 导弹 blk 样本 | `gszabi99/War-Thunder-Datamine` | 公开数据 dump | `missile_samples/` |
| atmosphere / polares / dynamicPhysModel | `GaijinEntertainment/DagorEngine` | BSD-3-Clause | `references/gaijin_dagor/` |
| wt_ballistics_calc | `Warthunder-Open-Source-Foundation/wt_ballistics_calc` | Apache-2.0 | `references/community_wt_ballistics_calc/` |
| WT-Projectile | `Alexmalab/projectile` | 未声明（保留所有权利） | `references/community_wt_projectile/` |

本项目原创的分析笔记与 README 以 **MIT** 许可证发布（见 `LICENSE`）。第三方代码保留各自许可证，详见各子目录内的 `LICENSE` 文件。

> 注：`Alexmalab/projectile` 上游未附带许可证文件。本项目仅以片段形式收录其源码用于**教育性参考**，著作权归原作者所有；任何超出教育/研究用途的再利用请先取得作者许可。

---

## 引用的资料

- War Thunder CDK 文档（Missile Variables）：<https://wiki.warthunder.com/>
- 1965 Stability DATCOM（WT 飞行模型气动方法论参考）
- 社区组织：<https://github.com/Warthunder-Open-Source-Foundation>

---

## 免责声明

本项目为个人技术研究档案，与 Gaijin Entertainment 无关。所载数据取自公开的 datamine 与开源引擎，仅用于学习导弹运动仿真算法；不提供、不鼓励对游戏客户端做任何修改或重新分发。若权利方认为任何内容不宜公开，可联系移除。
