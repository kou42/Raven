# Raven AstroWorld 実装ロードマップ

## 目的

Raven に天体力学を扱う新しい Physics Domain として `AstroWorld` を追加し、既存の Rigid Body / Electromagnetism と同じ fixed-step 境界の中で、N-body 重力・軌道運動・大規模相互作用・電磁気連携を段階的に実装するためのロードマップです。

この文書では、最初から大規模最適化へ進まず、**Direct N-body Solver を検証基準として成立させてから、座標精度・計測・Barnes–Hut・Electromagnetism 連携へ進む**ことを基本方針とします。

---

## 1. 現在の Raven Physics との接続方針

現在の `PhysicsSimulationWorld` は Physics Domain の fixed-step 順序を統括しています。

```text
PhysicsSimulationWorld
    |
    +-- ElectromagneticSystem
    +-- PhysicsWorld (Rigid Body)
    +-- FluidWorld
    +-- SoftBodyWorld
    +-- ThermalWorld
```

`ElectromagneticSystem` は位置や速度を直接変更せず、`RigidBodyComponent::Force` へ Electric / Magnetic / Coulomb Force を蓄積し、その後の `PhysicsWorld::Step()` が積分します。

`AstroWorld` も同じ責務境界に合わせます。

```text
PhysicsSimulationWorld
    |
    +-- AstroWorld
    |     |
    |     +-- Gravity Solver
    |     +-- Astro Spatial Structure
    |     +-- Orbital Diagnostics
    |
    +-- ElectromagneticSystem
    |
    +-- PhysicsWorld
          |
          +-- Force Integration
          +-- Collision / Contact Solver
```

基本契約は次の通りです。

- `AstroWorld` は天体相互作用と天体向け数値計算を担当する。
- 重力を既存 `ElectromagneticSystem` へ混在させない。
- 電場・磁場・Coulomb Force は既存 `ElectromagneticSystem` の責務として維持する。
- Rigid Body と共有するモードでは、可能な限り `RigidBodyComponent::Force` を共通の力集約点として利用する。
- Solver、Spatial Structure、ECS Component、World orchestration の責務を分離する。
- Demo 固有の太陽・地球パラメータを Runtime の汎用クラスへ埋め込まない。

---

## 2. 最終的に目指す構成

候補ディレクトリ:

```text
Root/Root/Raven/Physics/Astro/
    AstroWorld.h
    AstroWorld.cpp

    CelestialBody.h

    Gravity/
        GravitySolver.h
        DirectGravitySolver.h
        DirectGravitySolver.cpp
        BarnesHutGravitySolver.h
        BarnesHutGravitySolver.cpp

    Spatial/
        AstroOctree.h
        AstroOctree.cpp

    Tests/
        AstroWorldSelfTests.h
        AstroWorldSelfTests.cpp
```

実際にファイルを追加する段階では、既存 Physics ディレクトリ構造を再確認し、`structure.txt`、`Root.vcxproj`、`Root.vcxproj.filters` も同じ変更内で更新します。

---

## 3. データモデル方針

### CelestialBodyComponent

天体としての意味を ECS 側へ持たせます。

初期案:

```cpp
struct CelestialBodyComponent
{
    bool GenerateGravity = true;
    bool ReceiveGravity = true;
};
```

質量については、最初から `CelestialBodyComponent::Mass` を追加して二重管理しません。

実装開始時に現在の `RigidBodyComponent` の Mass / InverseMass 契約を確認し、

- Rigid Body と一体化する天体
- AstroWorld 内部だけで積分する大規模天体
- Static source として重力だけ生成する天体

のどこまで同じ質量ソースを共有するか決定します。

### AstroBodyState

Barnes–Hut や将来の独立積分器を見据え、Solver が ECS を直接走査し続ける構造にはしません。

候補:

```cpp
struct AstroBodyState
{
    EntityHandle Entity{};
    double Mass = 0.0;

    // 精度方針確定後に double precision vector を使用する。
    // Position / Velocity / Acceleration は Solver 用状態として明示的に分離する。
};
```

Scene から Solver 入力へ変換する境界を `AstroWorld` が所有します。

---

## 4. Phase 0 — 単位系・座標精度・責務の確定

### 目的

天体スケールを通常のゲーム物理へそのまま入れてから精度問題を修正するのではなく、最初に数値表現の境界を決めます。

### 実装前確認

- [ ] Raven Physics の長さ・時間・質量の単位契約を確認。
- [ ] `math::Vec3` の精度と `TransformComponent::Position` の型を確認。
- [ ] `RigidBodyComponent` の Mass / InverseMass / Force accumulator の型を確認。
- [ ] Gravity 系の既存 `PhysicsField` 実装と責務重複を確認。
- [ ] Scene Transform と Physics state の同期方向を確認。
- [ ] fixed-step の時間スケール変更が既存 Physics Domain に与える影響を確認。

### 方針

天体距離では float の絶対 world position が精度不足になる可能性が高いため、次のいずれかを明示的に選択します。

1. AstroWorld 内部のみ double precision。
2. Sector / Local Origin を持つ階層座標。
3. Floating Origin と Astro double state の併用。

初期実装では **AstroWorld 内部 double precision + Scene への相対座標変換**を第一候補とします。

### 完了条件

- [ ] Astro と通常 Scene の座標変換契約が文書化されている。
- [ ] 質量の正本が1か所に決まっている。
- [ ] SI 単位または Engine scale のどちらを使うか決まっている。
- [ ] `AstroWorld` が位置・速度を直接所有する範囲が決まっている。

---

## 5. Phase 1 — Direct N-body Gravity

### 目的

最適化されていない代わりに挙動を追いやすい (O(N^2)) Solver を最初の正解実装にします。

### Gravity Solver 境界

```cpp
class GravitySolver
{
public:
    virtual ~GravitySolver() = default;

    virtual void ComputeForces(
        /* body states */,
        /* output forces */) = 0;
};
```

Direct Solver:

```text
for each body A
    for each body B where B > A
        Compute F(A, B)
        Apply -F to A
        Apply +F to B
```

同一ペアの力を1回だけ計算し、作用・反作用を同時に蓄積します。

### 数値安全性

- [ ] ゼロ距離を処理。
- [ ] MinimumDistance / Softening の設定を追加。
- [ ] 非有限値を Force accumulator へ流さない。
- [ ] Mass <= 0 の扱いを明示。
- [ ] GenerateGravity / ReceiveGravity の組み合わせをテスト。

### Self Test

- [ ] 2体間重力の方向。
- [ ] 距離2倍で力が1/4。
- [ ] 質量2倍で力が2倍。
- [ ] 作用・反作用の和がほぼ0。
- [ ] Static gravity source から Dynamic body への引力。
- [ ] MinimumDistance 内で NaN / Inf が発生しない。

### 完了条件

Direct Solver の結果を、後続の Barnes–Hut / GPU Solver の Reference として使用できること。

---

## 6. Phase 2 — AstroWorld と PhysicsSimulationWorld の統合

### 目的

天体重力を Demo Layer から直接呼ばず、Raven の正式な fixed-step orchestration に組み込みます。

想定フロー:

```text
PhysicsSimulationWorld::StepSimulation()
        |
        +-- AstroWorld::AccumulateGravityForces()
        |
        +-- ElectromagneticSystem::ApplyElectricFieldForces()
        +-- ElectromagneticSystem::ApplyMagneticFieldForces()
        +-- ElectromagneticSystem::ApplyCoulombForces()
        |
        +-- PhysicsWorld::Step()
        |
        +-- Fluid / SoftBody / Thermal ...
```

この段階では、Gravity と Electromagnetism の順序に物理的な優先度を持たせず、**Rigid Body 積分前に同じ Force accumulator へ外力を揃える**ことを契約とします。

### API候補

```cpp
class AstroWorld
{
public:
    void AccumulateGravityForces(Scene& scene, float fixedDeltaTime);

    void SetGravitySolver(/* solver */);

    void Clear();

private:
    // Scene -> Astro state collection
    // Solver execution
    // Force feedback
};
```

### 完了条件

- [ ] `PhysicsSimulationWorld` が `AstroWorld` を永続所有。
- [ ] `GetAstroWorld()` から設定・診断へアクセス可能。
- [ ] Astro gravity が Rigid Body の既存積分を再利用。
- [ ] Force が fixed-step をまたいで意図せず残らない。
- [ ] 既存 Electromagnetism / Fluid / SoftBody / Thermal の順序を壊さない。

---

## 7. Phase 3 — 軌道検証と積分器評価

### 目的

「力の式が正しい」だけでなく、長時間積分したときの軌道誤差を確認します。

### 最初の検証シナリオ

- [ ] 2-body circular orbit。
- [ ] Elliptical orbit。
- [ ] 3-body の簡易ケース。
- [ ] 固定中心天体 + satellite。
- [ ] Escape velocity 前後。

### 診断値

- Total Energy
- Kinetic Energy
- Potential Energy
- Total Linear Momentum
- Angular Momentum
- Orbital radius error
- Period error

### 積分器

既存 Rigid Body integration を Reference として挙動を確認した後、天体向けに必要であれば次を比較します。

- Semi-Implicit Euler
- Velocity Verlet / Leapfrog
- Symplectic integrator

長時間軌道では energy drift が重要なため、通常ゲーム物理と同じ積分器を無条件に正式採用しません。

### 完了条件

- [ ] 代表軌道について許容誤差を数値で確認できる。
- [ ] 積分器ごとの energy drift を比較できる。
- [ ] Astro 専用積分器が必要か判断できる。

---

## 8. Phase 4 — Performance Counter

### 目的

Barnes–Hut を導入する前に、Direct Solver の実コストを測定します。

候補 Counter:

```cpp
struct AstroStatistics
{
    uint64_t ActiveBodyCount = 0;
    uint64_t GravityPairCandidateCount = 0;
    uint64_t GravityForceEvaluationCount = 0;

    double GravitySolveTimeMs = 0.0;
};
```

### 計測項目

- [ ] Active body 数。
- [ ] pair candidate 数。
- [ ] 実際の force evaluation 数。
- [ ] Scene -> Astro state collection 時間。
- [ ] Gravity solve 時間。
- [ ] Force feedback 時間。
- [ ] fixed-step 全体に占める Astro 比率。

### ベンチマーク規模

- 10 bodies
- 100 bodies
- 1,000 bodies
- 10,000 bodies

実行可能な範囲で Direct Solver の増加傾向を記録します。

### 完了条件

Barnes–Hut が必要になる body 数を Raven の実測値から判断できること。

---

## 9. Phase 5 — Barnes–Hut Octree

### 目的

長距離逆二乗相互作用を (O(N^2)) から概ね (O(N log N)) へ削減します。

### AstroOctree Node

候補:

```cpp
struct AstroOctreeNode
{
    // double precision bounds

    double TotalMass = 0.0;
    // CenterOfMass

    std::array<int32_t, 8> Children{};
};
```

Acceptance criterion:

```text
s / d < theta
```

- `s`: node size
- `d`: target body から node 代表点までの距離
- `theta`: 精度と速度のトレードオフ

### 実装順

- [ ] Octree build。
- [ ] TotalMass / CenterOfMass 集約。
- [ ] Gravity traversal。
- [ ] theta 設定。
- [ ] Direct Solver との誤差比較。
- [ ] Counter 追加。
- [ ] node allocation / rebuild cost 計測。

### 検証

同じ初期状態について

```text
DirectGravitySolver
        vs
BarnesHutGravitySolver
```

を比較します。

確認値:

- Force relative error
- Orbit drift
- Total energy drift
- Solve time
- Tree build time
- Visited node count
- Accepted aggregate node count

### 完了条件

速度だけでなく、Direct Solver に対する誤差を定量化した上で採用できること。

---

## 10. Phase 6 — Gravity / Coulomb Spatial Structure 共有の検討

### 目的

Gravity と Coulomb はどちらも長距離逆二乗相互作用であるため、Octree の空間分割基盤を共有できるか検討します。

将来の Node 候補:

```cpp
struct LongRangeInteractionNode
{
    double TotalMass = 0.0;
    // CenterOfMass

    double TotalCharge = 0.0;
    // CenterOfCharge
};
```

ただし、最初から Gravity と Electromagnetism を1つの巨大 Solver に統合しません。

共有対象は

- Tree topology
- Bounds
- body index partition
- traversal infrastructure

を中心とし、

- Gravity force law
- Coulomb force law
- 各 Domain の設定
- Statistics

は独立させます。

### 注意点

正負電荷が同一 node 内で相殺する場合、単純な TotalCharge 近似は Gravity の TotalMass より誤差特性が複雑です。

そのため Coulomb の Barnes–Hut 化は Gravity 版の完成後に、Direct Coulomb Solver と誤差比較して導入します。

---

## 11. Phase 7 — Electromagnetism 連携

### 目的

重力だけでなく、荷電粒子が天体磁場・電場から力を受けるシミュレーションへ拡張します。

総外力:

```text
Ftotal
    = Fgravity
    + qE
    + q(v x B)
    + Fcoulomb
```

既存の

- `ElectricChargeComponent`
- `ElectricField`
- `MagneticField`
- `ElectromagneticSystem`

を再利用します。

### DipoleMagneticField

惑星磁場の第一段階として磁気双極子場を追加します。

候補:

```cpp
class DipoleMagneticField final : public MagneticField
{
public:
    math::Vec3 Evaluate(
        const math::Vec3& worldPosition) const override;

private:
    math::Vec3 m_Center{};
    math::Vec3 m_DipoleMoment{};
};
```

ただし AstroWorld が `MagneticField` の所有者になるとは限りません。

Field の Lifetime / Ownership は既存 `ElectromagneticSystem` の非所有 Registry 契約に合わせて明示します。

### 検証候補

- [ ] Uniform B 中の charged particle。
- [ ] Dipole B 中の charged particle。
- [ ] Gravity + Magnetic Field。
- [ ] Gravity + Electric Field。
- [ ] Gravity + Lorentz + Coulomb。
- [ ] 同一 fixed-step 内で全外力が Rigid Body 積分前に揃うこと。

---

## 12. Phase 8 — Multi-rate Simulation / Physics LOD

### 目的

すべての相互作用を同じ頻度で再計算しない構造へ拡張します。

例:

```text
Rigid integration        60 Hz
Near gravity             60 Hz
Far gravity              15-30 Hz
Far electromagnetic      15 Hz
Visualization            render rate
```

### 実装状況

- [x] Near / Far interaction の更新頻度分離。
- [x] 遠距離 aggregate force のキャッシュ。
- [x] 確定済み Far force 間の transition smoothing。
- [x] Far force の新旧変化量を基準にした Adaptive Physics LOD。
- [x] Direct Reference による任意の誤差診断。
- [x] Direct / 固定 Multi-rate / Adaptive / Adaptive + Smoothing の長時間軌道回帰。
- [x] Near Direct hot path の pair 単位一時 allocation 除去。
- [ ] Sleeping / inactive Astro body。

Sleeping / inactive は既存 Rigid Body の sleep 契約との責務整理が必要なため、
Multi-rate / Physics LOD の必須完了条件から切り離し、後続最適化候補として残します。

### 完了条件

- [x] Near は毎 fixed-step、Far は低頻度で更新できる。
- [x] Near / Far 境界横断時に古い Far cache を誤適用しない。
- [x] Far 更新周期を物理量の変化に応じて自動調整できる。
- [x] Barnes-Hut の空間近似とは独立して時間 LOD の長時間誤差を回帰できる。
- [x] Direct Reference 診断を通常 Runtime の必須 O(N^2) コストにしない。

### 原則

Multi-rate 化は Barnes–Hut と同時に導入しません。

Direct → Barnes–Hut の誤差と、更新頻度低下による誤差を分離して測定できるよう、最適化を段階的に追加します。

---

## 13. Phase 9 — GPU / Particle-Mesh / 大規模 Simulation

CPU Barnes–Hut でも不足する規模になった場合のみ検討します。

候補:

- GPU Compute Direct N-body
- GPU Barnes–Hut
- Particle-Mesh
- Fast Multipole Method
- Plasma Particle Solver
- MHD Solver

この段階は通常の「天体 Entity 数百〜数千」よりも、宇宙塵・太陽風・プラズマなど大量粒子用途を対象とします。

既存 RigidBody を数十万個生成する設計にはせず、Particle Domain と Rigid/Astro Domain の境界を分離します。

---

## 14. Debug / Visualization

Astro 実装では数値の正しさを視覚的にも確認できるようにします。

候補:

- 軌道 Trail。
- Velocity vector。
- Gravity force vector。
- Center of Mass。
- Octree bounds。
- Barnes–Hut accepted node。
- Electric / Magnetic field vector。
- Energy / Momentum graph。
- Solver statistics panel。

Debug 描画は Astro Solver 本体へ Renderer 依存を入れず、診断データを Renderer / Debug Layer が読む構造を維持します。

---

## 15. テスト戦略

### Unit / Self Test

- Gravity formula。
- MinimumDistance。
- action / reaction。
- Octree aggregate。
- Barnes–Hut acceptance。
- Direct vs Barnes–Hut error。
- Dipole Magnetic Field。
- Gravity + EM force accumulation。

### Integration Test

- `PhysicsSimulationWorld` fixed-step。
- Force accumulator clear。
- Scene Transform synchronization。
- Rigid Body integration。
- Electromagnetism coexistence。

### Long-running Test

- Orbit period。
- energy drift。
- angular momentum drift。
- Barnes–Hut long-term error。

---

## 16. 実装順序

実装は次の順序を基本とします。

```text
Phase 0
単位系 / double精度 / 座標系 / 質量の正本
        |
        v
Phase 1
Direct N-body Gravity
        |
        v
Phase 2
AstroWorld + PhysicsSimulationWorld統合
        |
        v
Phase 3
軌道テスト + 積分器評価
        |
        v
Phase 4
Performance Counter
        |
        v
Phase 5
Barnes-Hut Octree
        |
        v
Phase 6
Gravity / Coulomb Spatial Structure共有検討
        |
        v
Phase 7
Dipole Magnetic Field + Electromagnetism連携
        |
        v
Phase 8
Multi-rate / Physics LOD
        |
        v
Phase 9
GPU / Particle-Mesh / Plasma / MHD
```

---

## 17. 最初の実装単位

最初のコード変更では範囲を広げず、次だけを実装します。

### Step 1

- `AstroWorld` の責務境界を追加。
- `CelestialBodyComponent` を追加。
- Direct Gravity Solver を追加。
- 2-body Self Test を追加。
- `PhysicsSimulationWorld` の Rigid Body 積分前へ重力蓄積を接続。

### Step 2

- Circular orbit test。
- Energy / Momentum diagnostics。
- double precision state の正式化。

### Step 3

- Gravity pair / solve time Counter。
- body 数を増やした benchmark。

ここまで完了してから Barnes–Hut へ進みます。

---

## 18. 非目標

初期 AstroWorld では次を同時に実装しません。

- General Relativity。
- 高精度 ephemeris。
- Atmospheric simulation。
- Full plasma simulation。
- MHD。
- GPU N-body。
- Barnes–Hut と Multi-rate の同時導入。
- Renderer 固有の天体表現。

まず Raven の Physics Domain として検証可能な Newtonian N-body 基盤を完成させます。

---

## 19. 完成時の責務境界

```text
Scene / ECS
    |
    +-- CelestialBodyComponent
    +-- ElectricChargeComponent
    +-- RigidBodyComponent
    |
    v
PhysicsSimulationWorld
    |
    +-- AstroWorld
    |     |
    |     +-- Direct / Barnes-Hut Gravity Solver
    |     +-- Astro Spatial Structure
    |     +-- Orbital Diagnostics
    |
    +-- ElectromagneticSystem
    |     |
    |     +-- Electric Field
    |     +-- Magnetic Field
    |     +-- Coulomb
    |
    v
Force / Astro State
    |
    v
Physics Integration
    |
    v
Scene Transform / Debug Visualization
```

`AstroWorld` は「宇宙に関係する処理を全部入れる箱」ではなく、**天体力学の状態収集・長距離重力 Solver・軌道診断・PhysicsSimulationWorld との接続を統括する Physics Domain**として維持します。

この境界を守ることで、Electromagnetism、Rigid Body、将来の Particle/Plasma Solver を独立したまま組み合わせられる構造を目指します。
