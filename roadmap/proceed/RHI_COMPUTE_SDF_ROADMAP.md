# Raven RHI Compute / GPU SDF / Physics Field 統合ロードマップ

更新日: 2026-11-17  
対象: `kou42/Raven` / `master`  
状態: **計画策定済み・実装未着手**（この文書の追加を除く）

## 1. 目的とスコープ

既存の OpenGL / DirectX 12 / Vulkan RHI と Physics の設計を維持しながら、Compute Shader 実行基盤、GPU SDF、汎用の空間 Field、流体・粒子・熱・電磁気連成を段階的に追加する。

- GPU SDF は **形状・境界・距離** を表す。磁場・電場・電流そのものを SDF と同一視しない。
- `PhysicsField` は「力場」ではなく **物理量を空間位置から評価する共通抽象** とする。
- `ScalarField` と `VectorField` は値型を区別する。物理単位の異なる Field を無条件に加算しない。
- Field（評価契約）、Storage（CPU Grid / GPU Texture / Sparse Volume）、Solver（生成・更新・時間発展）を分離する。
- CPU 既存 Solver と GPU 新 Solver は共存させ、GPU-only Field に暗黙の同期 CPU `Evaluate()` を要求しない。
- Graphics 本流と Frame Lifecycle を壊さない。DX12/Vulkan に Legacy `RHICommandList` を追加しない。
- Async Compute、全面 GPU Physics 移行、完全な時間依存 Maxwell Solver は初期スコープ外。

## 2. 既存資産と参照文書

### 確認済みの既存コード

- `Root/Root/Raven/Physics/Field/PhysicsField.h`: `PhysicsField`、`ScalarField::Evaluate(worldPosition)`、`VectorField::Evaluate(worldPosition)` が存在する。**既存 API を原則維持**。
- `Root/Root/Raven/Physics/Thermal/TemperatureField.h`: `TemperatureField : ScalarField` が存在する。既存の Influence / Blend / Priority 契約を維持する。
- `Root/Root/Raven/Renderer/RHI/RHIDevice.h`: Buffer / Texture / Graphics Pipeline 生成の入口が存在する。
- `Root/Root/Raven/Renderer/RHI/RHITypes.h`: `RHIBufferUsage::Storage` が定義されている（各 Backend の完全な Storage 対応を意味しない）。
- `Root/Root/Raven/Renderer/RHI/RHISceneFrameLifecycle.h`: `RHISceneCommandList` は現状 Graphics 中心の契約。
- `Root/Root/Raven/Platform/DirectX12/DX12SceneCommandList.h` と `Root/Root/Raven/Platform/Vulkan/VulkanSceneCommandList.h`: Explicit Scene 用 Command List Adapter。
- 既存の `FluidWorld`、SPH、`ThermalWorld`、Electromagnetism、および Particle 関連機能は維持する。

### 既存ロードマップとの関係

- `roadmap/proceed/RHI_MAINLINE_ROADMAP.md` を **RHI 本流統合の正本** とする。
- この文書は Compute / Field / SDF の **追加機能ロードマップ** とし、既存ロードマップの完了判定を上書きしない。
- 既存ロードマップには Debug / Release ビルドと Backend Smoke Test の未完了記録がある。着手時に最新 HEAD と実測で再判定する。

## 3. アーキテクチャ方針

```text
                         PhysicsField (既存・共通タグ)
                         /                       \
              ScalarField (既存)           VectorField (既存)
               /      |     \              /      |        \
             SDF   Temperature Potential  Electric Magnetic Velocity
               \         |       /          \       |       /
                CPU analytical / CPU grid / GPU field view
                              |
                      Field Storage Layer
                  Dense Grid / Sparse Grid / GPU Texture
                              |
                      Field Generator / Solver
                   SDF / Fluid / Thermal / Electromagnetic
                              |
                    RHI Compute Execution Layer
                     OpenGL / DX12 / Vulkan
```

### Field / Storage / Solver の責務

1. **Field**: 物理量と world-space 位置の評価契約。既存 `ScalarField` / `VectorField` を活用する。必要なら単位・意味を派生型で表現する。
2. **Storage**: CPU dense/sparse grid、GPU Texture / Buffer、座標変換、サンプリング、リソース寿命を管理する。Grid は物理法則を解かない。
3. **Solver**: SDF 構築、Poisson / 電位、流体、熱、磁気静力学などの数値計算を担当する。
4. **Coupling**: Field の物理量を質量・電荷・速度・材質に応じて力・熱源・境界条件へ変換する。

GPU-only Field に CPU `Evaluate()` を直接実装させない。CPU 評価が必要なら CPU ミラーまたは **明示的な Readback** を用意し、非同期完了と stale data の扱いを定義する。

### 電磁気での区別

- SDF: 導体・磁性体・絶縁体の領域と表面、境界法線。
- Scalar Field: 電位 `V`、導電率 `σ`、透磁率 `μ`、誘電率 `ε`、温度 `T`。
- Vector Field: 電場 `E`、磁束密度 `B`、電流密度 `J`、流速 `u`。
- Solver: `∇·(σ∇V)=0` などを解く。SDF だけでは磁場や電流は求まらない。
- Coupling: `F=q(E+v×B)`、`Q=J·E` などを物理単位に従って適用する。
- 複数材質には Material ID / Material Field を併用する。単一 SDF の符号だけで材質種別を決めない。

## 4. 実装フェーズと完了条件

### Phase 00 — 最新 HEAD / 基盤監査・ベースライン

- [ ] `master` HEAD、RHI 関連 PR、既存ロードマップ、実装との差分を確認。
- [ ] Debug / Release x64 のビルド状況を確認。未成功なら新機能追加前に修復。
- [ ] OpenGL / DX12 / Vulkan 通常 Application の Smoke Test と Validation を確認。
- [ ] `RHIDevice`、`RHISceneCommandList`、`IExplicitSceneRuntime`、Frame / Queue / Resource Ownership を調査。
- [ ] 3D Texture、Storage Buffer、Storage Image、Shader Asset、Readback の実装可否を調査。
- [ ] 既存 `PhysicsField`、Thermal、Electromagnetism、Fluid の Registry / Step 順序を記録。

**完了条件**: 既存 Graphics の回帰基準、Compute の不足 API、Backend 制約が文書化される。

### Phase 01 — PhysicsField / Field Storage の責務確定

- [ ] 既存 `PhysicsField` / `ScalarField` / `VectorField` API を維持する方針を確定。
- [ ] `SignedDistanceField : ScalarField` の CPU 契約を設計（符号・world-space・法線・境界条件）。
- [ ] `ScalarGrid3D` / `VectorGrid3D` の CPU 参照ストレージ、補間、Grid Transform を設計。
- [ ] GPU Field View / Storage を CPU 仮想 `Evaluate()` と分離。
- [ ] CPU/GPU 同期・Readback・更新世代・リソース寿命を定義。
- [ ] 電磁場、速度場、温度場の単位とサンプリング規約を整理。
- [ ] セル中心・セル面（MAC Grid 等）・ベクトル成分配置の拡張余地を残す。

**完了条件**: 既存 `TemperatureField` に破壊的変更なく、CPU Field と GPU Field を共通の上位概念で扱える設計とテスト方針が確定する。

### Phase 02 — RHI Compute 共通契約

- [ ] `RHIComputePipeline` / Specification と Shader 読込契約を設計。
- [ ] Compute Resource Binding（Storage Buffer / Image、Uniform、Sampled Texture）を設計。
- [ ] Dispatch、リソース遷移・Barrier、可視性、Readback、機能検出を定義。
- [ ] Explicit Scene と Legacy OpenGL の責務分離を維持。
- [ ] 初期は既存 Graphics Queue / Frame に統合。Async Compute Queue は対象外。
- [ ] Render Pass 内外の実行制約、DX12/Vulkan の Descriptor / Root Signature 対応を整理。

**完了条件**: 3 Backend が実装可能な最小共通 API と失敗時契約が定義される。

### Phase 03 — OpenGL Compute Smoke Test

- [ ] Compute Shader コンパイル / リンク、Storage Buffer Binding、Dispatch を実装。
- [ ] Memory Barrier と GPU→CPU Readback を実装。
- [ ] 既知の配列演算を CPU 参照値と比較。
- [ ] 3D Storage Texture 書込と描画側 Sampling の依存関係を確認。
- [ ] 既存 OpenGL Scene / UI / Resize の回帰を確認。

**完了条件**: GPU計算結果を CPU と比較でき、Graphics への受け渡しが正しく動く。

### Phase 04 — DirectX 12 / Vulkan Compute

- [ ] DX12 Compute PSO、Root Signature、SRV/UAV、Dispatch、UAV/Resource Barrier。
- [ ] Vulkan Compute Pipeline、Descriptor、`vkCmdDispatch`、Image Layout / Pipeline Barrier。
- [ ] GPU Resource lifetime、Frame内依存、Submit / Present を検証。
- [ ] 3 Backend で同一テストデータの結果を許容誤差内で比較。
- [ ] DX12 Debug Layer、Vulkan Validation Layer、Resize / Scene 切替を検証。

**完了条件**: 3 Backend で Compute + Graphics の Smoke Test が成功し、重大な Validation error がない。

### Phase 05 — GPU Dense SDF

- [ ] 解析的球 SDF を GPU 3D Texture に生成し CPU 参照と比較。
- [ ] Triangle Mesh → Unsigned Distance Field（Voxel-Centric）を実装。
- [ ] 閉じたメッシュの内外符号判定を実装。開いたメッシュは別契約にする。
- [ ] 境界法線、薄い形状、退化三角形、Transform、Voxel解像度を検証。
- [ ] SDF を可視化し、CPU/GPU 距離誤差を測定。

**完了条件**: 既知形状とメッシュで符号・距離・法線の誤差が規定値内に収まる。

### Phase 06 — Narrow-Band / Sparse SDF

- [ ] Triangle-Centric 候補範囲と Atomic Min 等の距離集約を検証。
- [ ] Narrow Band、Sparse Tile、Tile Lookup、タイル境界補間を実装。
- [ ] 距離の量子化（8-bit 等）は精度測定後に採否を決定。
- [ ] Incremental Update / フレーム分散を実装。
- [ ] Dense SDF に対してメモリ・GPU時間・距離精度を比較。

**完了条件**: 精度劣化を定量化したうえで、対象シーンに対する省メモリまたは高速化が確認できる。

### Phase 07 — Physics / Fluid / Particle 連携

- [ ] SDF を静的境界として使う Collision / Contact を追加（既存 Sphere / Box を維持）。
- [ ] `FluidWorld` と既存 CPU SPH を維持し、SDF 境界連携を追加。
- [ ] GPU Grid Fluid は別 Solver / Participant として導入。
- [ ] GPU 粒子と速度場のサンプリング・衝突を追加。
- [ ] CPU `std::vector<FluidParticle>` 前提の Coupling と GPU 状態を暗黙に混在させない。
- [ ] 同期タイミング、固定 Step、決定性、GPU Readback 頻度を検証。

**完了条件**: 既存 CPU Fluid の回帰がなく、GPU SDF 境界を用いた新規デモが動く。

### Phase 08 — Electromagnetism Field / Solver 連携

- [ ] SDF + Material ID から `σ/μ/ε` 等の Material Field を構築。
- [ ] 電位 Scalar Field と静電・定常電流 Solver を追加。
- [ ] 電場 `E=-∇V`、電流密度 `J=σE` を Vector Field として公開。
- [ ] 導体/絶縁体境界の条件、セル面係数、電流保存を検証。
- [ ] 磁気静力学 Solver と磁束密度 Field を段階追加。
- [ ] ローレンツ力連携とジュール熱源 `J·E` を追加。
- [ ] 解析解・電荷保存・電流保存・境界条件の誤差を検証。

**完了条件**: 既知の電極/導体/コイルの検証ケースで Field と Coupling が整合する。高周波 Maxwell / 渦電流 / 非線形磁性は別計画とする。

### Phase 09 — Thermal / Multiphysics 連携

- [ ] 既存 `TemperatureField` / `ThermalWorld` を維持。
- [ ] Grid Temperature Field と熱輸送・拡散を段階追加。
- [ ] 電磁気のジュール熱、流体移流、粒子熱源の Coupling を追加。
- [ ] 燃焼・着火・延焼は独立したモデルとして段階追加。
- [ ] 保存則、安定性、Step順序、CPU/GPU同期を検証。

**完了条件**: 電流→発熱→温度上昇、および流体→熱移流の独立テストが通る。

### Phase 10 — 計測・最適化・正式統合

- [ ] GPU Timestamp、Dispatch数、メモリ使用量、Readback待機を計測。
- [ ] Descriptor / Pipeline / Buffer 再利用、Sparse更新を最適化。
- [ ] 3 Backend、既存 Scene、Physics / Thermal / UI の回帰テスト。
- [ ] Debug / Release ビルド、GPU Validation、長時間動作を検証。
- [ ] API使用例、デモ、トラブルシュート、測定結果を文書化。

**完了条件**: 正確性・性能・リソース寿命の基準を満たし、既存機能の重大な回帰がない。

## 5. 依存関係と並行可能な作業

```text
Phase 00 基盤監査
    ├── Phase 01 Field / Storage 設計 ─────────────┐
    └── Phase 02 RHI Compute API                    │
              ↓                                    │
         Phase 03 OpenGL Compute                    │
              ↓                                    │
         Phase 04 DX12 / Vulkan Compute             │
              ↓                                    │
         Phase 05 Dense SDF  ←───────────────────────┘
              ↓
         Phase 06 Sparse SDF（最適化は必要に応じて後回し可）
              ├── Phase 07 Fluid / Particle
              └── Phase 08 Electromagnetism
                        ↓
                 Phase 09 Thermal / Multiphysics
                        ↓
                 Phase 10 最適化・正式統合
```

CPU の解析的 SDF と Field 参照実装は Phase 02〜04 と並行可能。Phase 07 / 08 は共通 Field Storage と Compute 同期契約が安定した後、独立に進められる。Phase 06 の全機能完了を Phase 07 / 08 の必須条件にはしない。

## 6. 実装規約とレビュー単位

- Phase ごとに小さな PR を基本とする。少なくとも Field契約、RHI共通API、Backend、SDF、Physics、電磁気を分離。
- 各 PR で **目的 / 対象ファイル / テスト / Backend差異 / 未解決リスク** を記録。
- CPU Reference と GPU 実装を比較し、浮動小数点許容誤差を明示。
- Compute→Graphics、Compute→Compute、Compute→Readback の同期を個別にテスト。
- Solver は Field を利用するが、Field の評価関数内で Solver の時間発展を起動しない。
- 実装していない API・テストを「実装済み」と記録しない。
- この文書はロードマップのみ。**現時点ではコード実装・実機検証は未実施**。

## 7. 次回着手時のチェックポイント

1. 最新 `master` と `RHI_MAINLINE_ROADMAP.md` の未完了項目を照合。
2. Debug / Release ビルドと 3 Backend Smoke Test の実測状況を確認。
3. `PhysicsField.h` と `TemperatureField.h` の互換性要件を固定。
4. `RHIDevice`、`RHISceneCommandList`、Explicit Runtime の Compute 統合点を確定。
5. Phase 00 の調査結果をこの文書へ追記し、Phase 01 の詳細設計へ進む。
