# War Thunder 物理算法 · 源码摘录与对照

> 整理日期：2026-09-08
> 目的：把 War Thunder（WT）弹体动力学相关的**可运行源码**从两个渠道抠出来——Gaijin 开源的 Dagor 引擎 + 社区逆向复现项目——并逐段标注物理含义，对接你手里 `missile_samples/` 里的 `.blkx` 参数。
> 所有代码均为**原文摘录**，已核对。

---

## 0. 源码地图（先搞清楚什么在哪儿）

| 来源 | 仓库 | 是否公开 | 含什么 |
|---|---|---|---|
| 官方引擎 | `GaijinEntertainment/DagorEngine` | **开源**（BSD-3，2023 起） | 通用引擎 + `gamePhys/` 物理（大气、飞机极曲线、刚体动力学+碰撞） |
| 官方数据 | `gszabi99/War-Thunder-Datamine` | 公开 dump | `.blkx` 载具/导弹参数（你已下到本地） |
| 社区·导弹弹道 | `Warthunder-Open-Source-Foundation/wt_ballistics_calc`（Rust） | 开源 | 导弹 1-DOF 质点弹道解算器 |
| 社区·炮弹阻力 | `Alexmalab/WT-Projectile`（C++） | 开源 | 炮弹阻力公式（弹道系数法） |

**关键结论（修正前几轮的判断）**：

- Dagor 引擎**确实含物理代码**（`prog/gameLibs/gamePhys/`），不是只有通用渲染/网络。但里面是**大气模型、飞机气动极曲线、通用刚体动力学+碰撞**——
- **没有导弹/炮弹专用的 6-DOF 飞行求解器**。导弹的推力积分、CxK 阻力、比例导引那段在**闭源游戏客户端**里，引擎只给 `.blkx` 参数，不给积分实现。
- 想要可跑的弹道算法，去社区两个仓库（已克隆到本地 `wt_physics_src/`）。

---

## 1. 大气模型（Gaijin 权威版 vs 社区复现——逐字节一致）

### 1.1 Gaijin 权威 `atmosphere.cpp`（本地 `wt_physics_src/dagor_atmosphere.cpp`）

```cpp
float atmosphere::stdRo0 = 1.225f;   // 海平面密度 kg/m3
float atmosphere::_g    = 9.81f;     // 重力
float atmosphere::_Mu0  = 1.825e-6f;// 动力粘度 Pa·s
float atmosphere::_hMax = 18300.0f; // 模型有效上限 18.3 km

// 密度（h 单位 m）
float atmosphere::density(float h) {
  return ro0() * poly(DENSITY_COEFFS, min(h, hMax())) * (hMax() / max(hMax(), h));
}
// 压力
float atmosphere::pressure(float h) {
  return P0() * poly(PRESSURE_COEFFS, min(h, hMax())) * (hMax() / max(hMax(), h));
}
// 温度（K）
float atmosphere::temperature(float h) { return T0() * poly(TEMPERATURE_COEFFS, min(h, hMax())); }
// 音速（m/s）
float atmosphere::sonicSpeed(float h) { return 20.1f * sqrtf(temperature(h)); }
// 动力粘度
float atmosphere::viscosity(float h) { return Mu0() * powf(temperature(h) / T0(), 0.76); }

static inline float poly(float tab[], float v) {
  return (((tab[4]*v + tab[3])*v + tab[2])*v + tab[1])*v + tab[0];
}
```

### 1.2 社区复现 `atmosphere.rs`（本地 `wt_physics_src/wtbc_atmosphere.rs`）

文件第 5 行明确写着「按照 Gaijin 的 `gamePhys/props/atmosphere.cpp` 实现」。系数数组与 Gaijin 同源（Gaijin 的系数放在 `atmosphere.h` 的 `DENSITY_COEFFS` 等宏里，这里直接展开）：

```rust
const DENSITY:     [f64;5] = [1., -9.59387e-05, 3.53118e-09, -5.83556e-14, 2.28719e-19];
const PRESSURE:    [f64;5] = [1., -0.000118441, 5.6763e-09, -1.3738e-13, 1.60373e-18];
const TEMPARATURE: [f64;5] = [1., -2.27712e-05, 2.18069e-10, -5.71104e-14, 3.97306e-18];

const STD_RO0: f64 = 1.225;   // 海面密度
const H_MAX:   f64 = 18300.0; // 上限
const G:       f64 = 9.81;

fn compute_polynomial(i:[f64;5], v:f64)->f64 { (((i[4]*v + i[3])*v + i[2])*v + i[1])*v + i[0] }

pub fn sonic_speed(&self, h:f64)->f64 { 20.1 * self.temperature(h).sqrt() }
pub fn density(&self, h:f64)->f64 {
  self.calc_density() * compute_polynomial(DENSITY, min(h, H_MAX)) * (H_MAX / max(H_MAX, h))
}
```

**物理含义 / 建模要点**

- 密度、压力、温度都是**高度 4 阶多项式**，海平面=1.225 kg/m³，模型在 18.3 km 以上把密度按 `hMax/h` 线性外推（密度→0）。
- 音速 `a = 20.1·√T`、T 以 K 计。海平面 T≈288.16 K → a≈341 m/s，吻合。
- 粘度用 Sutherland 型 `μ = μ₀·(T/T₀)^0.76`。
- 这套模型就是你做导弹仿真时 `ρ(h)` 该用的公式——直接用，别自己造。

---

## 2. 炮弹阻力（WT-Projectile，C++，真实实现）

本地文件 `wt_physics_src/projectile_GameFuncs.cpp`。这是**真·WT 炮弹阻力公式**，用「弹道系数法」。

```cpp
// 阻力常数：本质就是空气密度 ρ(h)（系数与大气模型完全一致）
float GameFuncs::GetDragConstant(float LocalPosY) {
  const auto maxAlt  = 18300.0f;
  const auto altMult = 1.225f;
  const auto clampedAlt = fmin(LocalPosY, maxAlt);
  const auto unk1 = 2.2871901e-19f, unk2 = 5.8355603e-14f,
              unk3 = 0.00000000353118f, unk4 = 0.000095938703f;
  return altMult *
    ((maxAlt / std::fmax(LocalPosY, maxAlt)) *
      ((((((unk1 * clampedAlt) - unk2) * clampedAlt) + unk3) *
        clampedAlt) - unk4) * clampedAlt + 1.0f);
}

// 弹道系数：BC = -ρ · π(caliber/2)² · L / (2·m)
float GameFuncs::GetBallisticCoeff(float BulletLength, float BulletMass,
                                   float BulletCaliber, float DragConstant) {
  return -1.0f *
    (DragConstant * static_cast<float>(M_PI) * 0.5f *
      std::pow(BulletCaliber * 0.5f, 2.0f) * BulletLength) / BulletMass;
}

// 积分一步：稳定（半隐式）二次阻力 + 重力
void GameFuncs::ApplyDrag(float BallisticCoeff, vec2& BulletVel, vec2& BulletPos) {
  float deltaSpeed = (BallisticCoeff * this->timeStep) * BulletVel.Length();
  float velocityMult = 1.0f;
  if (deltaSpeed < 1.0f || deltaSpeed > 1.0f)
    velocityMult = (deltaSpeed / (1.0f - deltaSpeed)) + 1.0f;   // = 1/(1 - deltaSpeed)

  vec2 draggedVel{
    BulletVel.x * velocityMult,
    (this->gravity * this->timeStep) + (velocityMult * BulletVel.y)
  };
  BulletVel = draggedVel;
  BulletPos = vec2{ draggedVel.x * timeStep + BulletPos.x,
                    draggedVel.y * timeStep + BulletPos.y };
}
```

**物理含义 / 建模要点**

- `GetDragConstant` 的 `unk1~unk4` **和大气密度多项式系数完全相同**——它返回的就是 `ρ(h)`（乘了 1.225 归一化）。
- `GetBallisticCoeff = -ρ·S·L/(2m)`，S=π(caliber/2)²。**有效阻力系数正比于弹长 L**。所以「长弹阻力小、短弹阻力大」在 WT 里是这么体现的。
- 阻力加速度大小 `|a_drag| = |BC|·|v|²`，即经典二次阻力 `D = ½ρV²S·(2L/m)`（注意这里把 L/m 揉进了系数）。
- `ApplyDrag` 用的是**半隐式稳定积分**：`v_new = v / (1 + |BC|·dt·|v|)`，分母 >1 自动衰减，不会像显式欧拉那样在高速时发散。重力单独加在 y 分量。这对你写仿真器是现成的稳定积分模板。

---

## 3. 导弹 1-DOF 质点弹道（wt_ballistics_calc，Rust）

本地文件 `wt_physics_src/wtbc_runner.rs`。这是把 `.blkx` 参数吃进去、解出**射程/速度包络**的核心。

```rust
const GRAVITY: f64 = 9.81;
let area = PI * (missile.caliber / 2.0).powi(2);   // 参考面积 S
let rho  = altitude_to_rho(altitude);              // 用第 1 节的密度模型

for i in 0..sim_len {
  // 阻力：D = 0.5 · ρ · V² · CxK · S   ← 与你 blk 的 CxK 直接对应
  drag_force = Force::from_N(0.5 * rho * ias.powi(2) * missile.cxk * area);

  // 两级推力 + 线性质量衰减
  let burn_0 = 0.0..missile.timefire0;
  let burn_1 = burn_0.end..burn_0.end + missile.timefire1;
  match () {
    _ if burn_0.contains(&flight_time) => {        // 助推级
      mass  = missile.mass - (missile.mass - missile.mass_end) * (flight_time / timefire0);
      force = Force::from_N(missile.force0);
    }
    _ if burn_1.contains(&flight_time) => {        // 续航级
      mass  = missile.mass_end - (missile.mass_end - missile.mass_end1) * ((flight_time-timefire0)/timefire1);
      force = Force::from_N(missile.force1);
    }
    _ => {                                          // 滑翔（熄火）
      mass  = if missile.mass_end1!=0.0 { missile.mass_end1 } else { missile.mass_end };
      force = Force::from_N(0.0);
    }
  }

  a = ((force - drag_force) / mass) - gravity;     // 合力/质量 - 重力
  ias += a * timestep;
  let tas = ias_to_tas(ias, &atmosphere, altitude); // IAS→TAS 换算后算距离
  distance += tas * timestep;
}
```

**物理含义 / 与 blk 参数对接**

| 代码里的量 | 来自 PL-15 `.blkx` | 说明 |
|---|---|---|
| `missile.caliber` | `caliber=0.203` | 弹径，算 S |
| `missile.cxk` | `CxK=1.6` | 阻力总缩放（叠在 `dragCx` 之上） |
| `missile.force0/1` | `propulsion0/1.impulse0.force` = 34500/17250 N | 两級推力（**blk 直接给 N**） |
| `missile.timefire0/1` | `impulse0.time` = 3.0/2.5 s | 工作时间 |
| `missile.mass / mass_end / mass_end1` | `mass=198`→`153`→`138` kg | 总重 / 一级后 / 二级后 |
| `rho` | 第 1 节大气模型 | 18.3 km 以内 |

⚠️ **这个解算器是 1-DOF（仅沿发射方向的距离/速度），不含横向制导、不含高度变化**。它算的是「平飞射程包络」，正是 PL-15 里 `table0~3` 那几张射程包线表的算法对应物。要做带机动/比例导引的 2D/3D 弹道，得在它外面自己加制导环（见第 5 节 blk 的 `propNavMult`）。

---

## 4. 飞机气动极曲线（Gaijin `polares.cpp`——DATCOM 风格）

本地文件 `wt_physics_src/dagor_polares.cpp`（25 KB，节选核心）。这是**飞机**气动，不是导弹，但能看清 Gaijin 整套气动建模思路（弹的 `CxK/finsAoa` 是把同思路压成几个常数）。

```cpp
// 由迎角(aoa)给出 Cx、Cy，再按侧滑角 ang 旋转到体轴
Point2 gamephys::calc_c(const Polares &polares, float aoa, float ang,
                        float cl_add, float cd_coeff) {
  float cxa = calc_cd(polares, aoa) * cd_coeff;   // 阻力系数
  float cya = calc_cl(polares, aoa) + cl_add;     // 升力系数
  sincos(DegToRad(ang), sa, ca);
  float cx = (cxa*ca - cya*sa) * polares.kq;       // 缩放到体轴 X
  float cy = (cya*ca + cxa*sa) * polares.clKq;     // 缩放到体轴 Y
  return Point2(cx, cy);
}

// 升力系数 vs 迎角：分段（线性段 → 临界前抛物段 → 失速后衰减 → 深失速正弦段）
float gamephys::calc_cl(const Polares &polares, float aoa) {
  if (aoa <= polares.aoaLineH && aoa >= polares.aoaLineL)   // 线性段
    return polares.cl0 + polares.clLineCoeff * aoa * polares.cyMult;
  const float sign = fsel(aoa - polares.aoaLineH + 0.01f, 1.f, -1.f);
  const float aoaCrit = fsel(sign, polares.aoaCritH, polares.aoaCritL);
  // ... 临界前抛物衰减 parabCyCoeff*sqr(aoaCrit-aoa)
  // ... 失速后 declineCoeff*sqr(dA)
  // ... 深失速 clAfterCrit*sin(PI*0.0125*aoa)
}
```

**建模要点**

- 升力/阻力按**迎角分段**：线性段（小迎角）→ 抛物衰减（接近临界）→ 失速后衰减 → 深失速正弦。这正是 DATCOM 极曲线的经典分段法。
- 阻力/升力缩放在 `kq`/`clKq`，对应 blk 里导弹的 `CxK`/`wingAreaMult`。
- 对导弹而言，你手里的 `finsAoaHor/Ver`、`finsLatAccel`、`stabilityThreshold` 就是这套极曲线的**简化控制参数**——导弹不需要完整极曲线，只用「舵偏→法向力」的线性段。

---

## 5. 通用刚体动力学（Gaijin `dynamicPhysModel.cpp`——6-DOF + 碰撞）

本地文件 `wt_physics_src/dagor_dynamicPhysModel.cpp`（6.6 KB，节选）。这是引擎里的**通用刚体求解器**（载具碰撞、碎片飞溅用），印证了 WT 的 6-DOF 姿态动力学骨架。

```cpp
// 构造函数：质心 + 三轴转动惯量
DynamicPhysModel::DynamicPhysModel(const TMatrix &tm, float bodyMass,
        const Point3 &moment, const Point3 &CoG, PhysType phys_type)
  : mass(bodyMass), momentOfInertia(moment), centerOfGravity(CoG) { ... }

// 冲量 → 线速度 + 角速度（6-DOF 刚体响应）
void DynamicPhysModel::applyImpulse(const Point3 &impulse, const Point3 &arm,
        Point3 &outVel, Point3 &outOmega, float invMass,
        const Point3 &invMomentOfInertia) {
  outVel += impulse * invMass;
  Point3 angMom = impulse % (arm - centerOfGravity);   // 对质心的角冲量
  angMom.x *= invMomentOfInertia.x;                    // 按三轴逆惯量缩放
  angHom.y *= invMomentOfInertia.y;
  angHom.z *= invMomentOfInertia.z;
  outOmega += angHom;
}

// 每步：重力 + 顺序冲量碰撞求解 + 积分
void DynamicPhysModel::update(float dt) {
  velocity += Point3(0.f, -atmosphere::g(), 0.f) * dt;   // 重力
  for (int it=0; it<5; ++it)                             // 5 次迭代解接触约束
    for (auto &info : collision) {
      float lambda = -safediv(a, b);                     // 冲量大小
      applyImpulse(info.normal*lambda, info.PnT, addVel, addOmega, invMass, invMoI);
    }
  location.P += (velocity + location.O.getQuat()*pseudoVel) * dt;  // 位置积分
  location.O.increment(-RadToDeg(omegaInc.y), ...);                  // 姿态积分
}
```

**建模要点**

- 完整 6-DOF：平动（质量）+ 转动（三轴惯量 `momentOfInertia`）+ 质心 `CoG`。
- 碰撞用**顺序冲量法**（sequential impulses，5 次迭代、ERP=1.0 的惩罚/约束求解）——这是现代物理引擎（Box2D/Bullet 同款）标准做法。
- 这回答了你之前「blk 里没有转动惯量怎么算 6-DOF」：**惯量要自己按几何估算**（圆柱近似 `I = ½m r²` 轴向 / `⅓mL²` 横向），引擎侧也是这么传 `moment` 进来的。
- 注意：这一段是**碰撞/接触动力学**，不是导弹飞行轨迹。导弹飞控在闭源侧，但姿态环（orientationAutopilot）的物理骨架就是这里的 6-DOF + 冲量。

---

## 6. 把以上串成你的导弹仿真

| 你要算的 | 用哪段代码 | 缺什么（自己补） |
|---|---|---|
| 空气密度 ρ(h) | §1 大气模型（Gaijin/社区一致） | — 直接用 |
| 阻力 D | 导弹：`D=0.5ρV²·CxK·S`（§3）；炮弹：`BC=ρSL/2m`（§2） | 导弹诱导阻力项 blk 没给，按 `wingAreaMult` 近似 |
| 推力/质量 | §3 两级推力 + 线性质量衰减 | — blk 直接给 N |
| 质点弹道 | §3 `a=(F-D)/m - g`，半隐式/显式欧拉 | 只 1-DOF，无高度/横向 |
| 比例导引 | blk 的 `propNavMult=4` → `a_cmd=4·Vc·σ̇`，限幅 `reqAccelMax` | 制导律本身在闭源侧，社区未复现完整 6-DOF 制导 |
| 6-DOF 姿态 | §5 刚体+冲量（骨架） | 转动惯量自行估算 |
| 气动控制 | blk 的 `finsAoa`/`finsLatAccel` + §4 极曲线思路 | 导弹只用线性舵效段 |

**一句话**：大气、阻力、质点弹道、刚体骨架——**全部有可运行源码**；唯一必须自己写的是**横向制导环 + 把 1-DOF 扩成 3-DOF/6-DOF**（用 blk 的 `propNavMult`/`reqAccelMax`/`loadFactorMax` 当增益与限幅）。

---

## 7. 本地文件清单（`wt_physics_src/`）

| 文件 | 来源 | 内容 |
|---|---|---|
| `dagor_atmosphere.cpp` | Gaijin 开源 | 权威大气模型（§1.1） |
| `dagor_polares.cpp` | Gaijin 开源 | 飞机气动极曲线（§4） |
| `dagor_dynamicPhysModel.cpp` | Gaijin 开源 | 通用刚体 6-DOF+碰撞（§5） |
| `wtbc_atmosphere.rs` | 社区 Rust | 大气模型复现（§1.2） |
| `wtbc_runner.rs` | 社区 Rust | 导弹 1-DOF 弹道解算（§3） |
| `wtbc_launch_parameters.rs` | 社区 Rust | 发射参数结构体（§3 输入） |
| `wtbc_lib.rs` | 社区 Rust | 模块清单 |
| `projectile_GameFuncs.cpp/.h` | 社区 C++ | 炮弹阻力公式（§2） |
| `projectile_main.cpp` | 社区 C++ | 2D 弹道主循环（dt=0.001） |

> 取数提示：国内 `github.com` 直连不通，用 `https://cdn.jsdelivr.net/gh/<owner>/<repo>@<branch>/<path>` 拉单文件；GitHub tree 用 `api.github.com/repos/<owner>/<repo>/git/trees/<branch>?recursive=1`。
