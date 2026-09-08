# 从 War Thunder Datamine 提取导弹运动仿真参数 —— 调研笔记

> 整理自与「墨」的对话（2026-09-07 ~ 09-08）。
> 目的：为**导弹运动模拟算法**找一份真实、可读的气动 / 运动参数数据源。
> 结论先行：Gaijin 的客户端资源被 gszabi99 解包后，导弹的物理参数就在 `aces.vromfs.bin_u/gamedata/weapons/` 下的 JSON 文件里，几乎含了做弹道 + 制导仿真要的全部量。

---

## 1. War-Thunder-Datamine 是什么

- 仓库：`https://github.com/gszabi99/War-Thunder-Datamine`，维护者 gszabi99（加几位贡献者）。
- 干的事：每次《战争雷霆》游戏客户端更新，把里面的资源包（专有格式 `.vromfs.bin_u`）解包、提取、原样提交到 GitHub。**纯数据 dump，只读客户端，不改游戏**。
- 规模：最早提交可追溯到 2022-09，至今 4000+ commits，最新版本 2.58.0.21（2026-09-04），基本游戏一更它就更。
- 价值：是 WT 社区"泄露"文化的源头（新载具/平衡改动提前曝光），也是所有第三方工具（wiki、性能对比、击杀信息条 MOD）的底层数据底座。
- 边界：`game.vromfs.bin_u` 等包带 EULA 灰区，但只做只读分析、不重新分发游戏资源本身，风险很低，Gaijin 长期默许。

---

## 2. 导弹数据在哪个目录

核心目录只有一个：**`aces.vromfs.bin_u/gamedata/weapons/`**（注意不是旧版认知里的 `game.vromfs.bin_u/config/weapons/`——那个目录在新版已经没了，武器定义整体挪进了 `aces` 包）。

| 子目录 | 内容 |
|---|---|
| `rocketguns/` | 载具 / 直升机发射的导弹与火箭弹，**按型号拆成独立 `.blkx`**（空空、空地、反坦、反舰……最全，PL-15、R-73、Kh-29 都在这） |
| `human_weapons/bullets/` | 步兵 / 载员便携式弹：`9m39`(针)、`fgm_148_javelin`、`fim_92e_stinger` 等 |
| `groundmodels_weapons/` | 地面载具导弹发射器定义 |

> 排除项：`aces.vromfs.bin_u/gamedata/flightmodels/*_missile_test.blkx` 和 `weaponpresets/*_missiles.blkx` 是**挂载测试配置**，不是导弹本体参数，别盯着它们。
>
> 旁证：`game.vromfs.bin_u` 顶层只有 `danetlibs / daslib / game / gamecommon / gamelibs / wtlibs`，没有 `config` 目录——确认旧版 `config/weapons/missiles.blk` 已不存在。

---

## 3. 文件格式：`.blkx` 是 JSON 文本

`gamedata/weapons` 下的 `.blkx` 文件头是 `{"nor...`，**完全可读的 JSON**，不是二进制 blk。可以直接 `json.load` 解析，无需任何解码工具。

每个导弹文件顶层大致是：

- `rocket` 块 —— **弹体本体**（质量、推力、气动、速度、制导、伤害、特效……）
- `rocketGun` / `shotFreq` / `mesh` / `tags` 等 —— 挂载 / 发射器定义，非弹体模型参数

---

## 4. PL-15 气动与运动建模参数清单

以 `cn_pl15_missile.blkx`（主动雷达中距空空弹）为例。文件里几百个字段，真正和**空气动力学 + 运动建模**相关的都在 `rocket` 块和 `rocket/guidance/*` 子块；那一大堆 `collisions/*`、`damage/*`、`*SmokeFx/*`、`proximityFuse/*` 全是伤害判定、碰撞特效、烟雾粒子、引信——**跟运动无关，建模直接忽略**。

### A. 质量 / 几何 / 惯性（弹体本体）

| 字段 | 值 | 物理意义 / 建模用途 |
|---|---|---|
| `mass` | 198.0 kg | 发射总重（积分初值） |
| `caliber` | 0.203 m | 弹径，算参考面积 `S = π·(d/2)²` |
| `length` | 3.93 m | 弹长 |
| `distFromCmToStab` | 0.3 m | **质心到安定面距离**——气动静稳定臂，算静稳定度 / 恢复力矩 |
| `warheadMass` | 30.0 kg | 战斗部质量 |
| `explosiveMass` | 9.2 kg | 装药质量 |

> ⚠️ 文件**没有转动惯量**（Ixx/Iyy/Izz）字段，做完整 6-DOF 姿态动力学得自己按几何估算。

### B. 推力 / 推进（直接给力，不用反推）

| 字段 | 值 | 说明 |
|---|---|---|
| `propulsion0/impulse0/force` | **34500 N** | 一级推力 |
| `propulsion0/impulse0/time` | 3.0 s | 一级工作 3 秒 |
| `propulsion0/impulse0/massLost` | 45.0 kg | 一级烧掉 45 kg 燃料 |
| `propulsion1/impulse0/force` | **17250 N** | 二级推力 |
| `propulsion1/impulse0/time` | 2.5 s | 二级工作 2.5 秒 |
| `propulsion1/impulse0/massLost` | 15.0 kg | 二级烧掉 15 kg |

**推力曲线 = 分段常数**：0–3 s 用 34500 N，3–5.5 s 用 17250 N，之后 0。质量按耗量线性减（198 → 153 → 138 kg）。

> 纠正一次误判：PL-15 **直接给了 `force`**，不需要用 `massLost/time` 反推推力。9M39 那颗才是只有 `timeFire`+`massEnd` 没给 force，那颗才需反推。两弹不要混。

### C. 空气动力（阻力 / 升力 / 稳定）

| 字段 | 值 | 物理意义 / 建模用途 |
|---|---|---|
| `dragCx` | 0.018 | **零升阻力系数基准** |
| `CxK` | 1.6 | 阻力系数总缩放乘子（实际 `Cx = dragCx·CxK·(1+诱导项)`） |
| `wingAreaMult` | 1.4 | 翼面积乘子，影响 S 与诱导阻力 / 升力 |
| `finsAoaHor` / `finsAoaVer` | 0.375092 | **舵面迎角效率**（舵偏角 → 法向力系数斜率，横 / 纵） |
| `finsLatAccel` | 41.4036 | 可用横向加速度上限（舵效上限，单位见第 6 节警告） |
| `stabilityThreshold` | 0.05 | 气动静稳定阈值（安定面稳定裕度） |

阻力项：`D = 0.5·ρ·V²·S·Cx`，`S` 用 `caliber`+`wingAreaMult` 推出，`Cx` 用 `dragCx·CxK` 起算。控制法向力由比例导引指令 + `finsAoa` 决定，受 `finsLatAccel` 限幅。

### D. 速度 / 射程 / 时间包络

| 字段 | 值 | 说明 |
|---|---|---|
| `useStartSpeed` / `startSpeed` | True / 0.0 | 离轨初速 = 0（纯助推起飞） |
| `endSpeed` | 2000 m/s | 发动机熄火末速 |
| `machMax` | 4.0 | 最大马赫数（速度上限约束） |
| `maxDistance` / `rangeMax` | 80000 m | 最大射程 80 km |
| `minDistance` | 30 m | 最小发射 / 引信安全距离 |
| `timeLife` | 80 s | 自毁 / 最大飞行时间 |
| `loadFactorMax` | 38.0 | 最大载荷因子（过载上限，单位 g） |

> PL-15 这文件里**没有 `maxSpeed` 字段**（9M39 有），速度上限由 `machMax` 和 `endSpeed` 共同约束。

### E. 制导与控制律（轨迹形状的核心）

| 字段 | 值 | 物理意义 |
|---|---|---|
| `guidanceType` | radar | 主动雷达末制导 |
| `guidanceAutopilot/propNavMult` | **4.0** | **比例导引系数 N**（PN=4，经典值，直接进导引方程 `a = N·Vc·σ̇`） |
| `guidanceAutopilot/reqAccelMax` | 38.0 | 最大指令加速度（限幅） |
| `guidanceAutopilot/accelControl{Prop,Intg,Diff}` | 0.0046 / 0.0375 / 0.00015 | 加速度控制 PID 增益 |
| `guidanceAutopilot/baseIndSpeed` | 1800.0 | 基准指示空速 |
| `guidanceAutopilot/loftEnabled` / `loftElevation` | True / 20.0° | **抛射弹道**：助推段抬升 20° |
| `loftTargetElevation` / `loftAngleToAccelMult` / `loftTargetOmegaMax` | -2.25 / 20.0 / 0.5 | 抛射段目标俯角 / 角-加速度增益 / 最大角速率 |
| `guidance/orientationAutopilot/*` | （角速率、控制增益） | 姿态指向自动驾驶，决定转向响应 |
| `guidance/inertialNavigation` / `inertialGuidance/datalink` | True / True | 惯导 + 双向数据链（中制导修正） |
| `inertialNavigationDriftSpeed` | 2.0 m/s | 惯导漂移速度（中段误差源） |

### F. 导引头 / 传感器（约束末段运动）

| 字段 | 值 | 说明 |
|---|---|---|
| `radarSeeker/active` | True | 末段主动雷达 |
| `radarSeeker/lockAngleMax` / `angleMax` | 60° | 最大离轴锁定角 |
| `radarSeeker/rateMax` | 60 °/s | 跟踪角速率上限（限制机动响应） |
| `radarSeeker/receiver/range` / `rangeMax` | 16 / 25 km | 导引头作用距离 |

---

## 5. 怎么用这些量搭运动仿真

1. **纵向推力段**：`F(t)` 分段常数（34500 → 17250 → 0），`m(t)` 按 45/15 kg 线性递减。
2. **阻力**：`D = 0.5·ρ·V²·S·(dragCx·CxK)`，`S` 由 `caliber`+`wingAreaMult` 估算。
3. **控制力**：比例导引出 `a_cmd = N·Vc·σ̇`，限幅到 `reqAccelMax=38` / `loadFactorMax=38`，舵效上限 `finsLatAccel`。
4. **弹道分段**：loft 抛射（抬 20°）→ 中段惯导 + 数据链（`driftSpeed=2` 引入误差）→ 末段 radar 主动（`rateMax=60°/s`、`lockAngleMax=60°`）。
5. **终止**：`timeLife=80 s` 或 `maxDistance=80 km` 或命中。

---

## 6. 两个必须注意的坑

- **单位无标注**：WT 的 blk 所有量都不标单位，全靠引擎内部约定。`loadFactorMax=38` 明确是过载(g)；`finsLatAccel=41.4` 在 WT 约定里通常是可用横向加速度——**是 m/s² 还是 g，得查 WT 源码或拿实弹轨迹反校**，别直接当 g 用。推力 `force` 是 N、质量 kg、长度 m、时间 s、角度 ° 这些基本可靠。
- **缺转动惯量**：做完整 6-DOF 姿态动力学时 `I` 需要自己按圆柱近似估算，文件没给。

---

## 7. 怎么拿到任意导弹文件

国内 `raw.githubusercontent.com` 不稳，改用 jsDelivr CDN 抓单文件：

```
https://cdn.jsdelivr.net/gh/gszabi99/War-Thunder-Datamine@master/aces.vromfs.bin_u/gamedata/weapons/rocketguns/<型号>.blkx
```

例（PL-15）：
```
https://cdn.jsdelivr.net/gh/gszabi99/War-Thunder-Datamine@master/aces.vromfs.bin_u/gamedata/weapons/rocketguns/cn_pl15.blkx
```

想批量看目录结构，用 GitHub 的 tree API：
```
https://api.github.com/repos/gszabi99/War-Thunder-Datamine/contents/aces.vromfs.bin_u/gamedata/weapons/rocketguns
```

---

## 附：本 workspace 已存的样本

| 文件 | 说明 |
|---|---|
| `missile_samples/9m39_igla_missile.blkx` | 肩射红外弹（9M39 针式），只有 `timeFire`+`massEnd` 无 `force`，需反推推力 |
| `missile_samples/cn_pl15_missile.blkx` | 主动雷达中距弹（PL-15），含 loft 抛射 + 双向数据链 + 三级射程包线表，直接给 `force` |

> 下一步可选：写 Python 提取器，把任意导弹 blk 自动归成「质量-推力-气动-制导」四张表，并生成一个可跑的简化弹道（比例导引 + 两级推力 + loft 分段）仿真骨架。
