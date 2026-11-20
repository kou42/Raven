# Raven RHI Compute / GPU SDF / Physics Field 統合ロードマップ

更新日: 2026-11-19

対象: `kou42/Raven` / `master`

状態: **Phase 00〜06 の Compute / Dense・Sparse SDF を確認済み。Phase 07 の Physics / Fluid / Particle 連携を実装・検証済み**

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

- [x] `master` HEAD、RHI 関連 PR、既存ロードマップ、実装との差分を確認。
- [x] Debug / Release x64 のビルド状況を確認。未成功なら新機能追加前に修復。
- [x] OpenGL / DX12 / Vulkan 通常 Application の起動・終了 Smoke Test と取得可能な Validation を確認。
- [x] `RHIDevice`、`RHISceneCommandList`、`IExplicitSceneRuntime`、Frame / Queue / Resource Ownership を調査。
- [x] 3D Texture、Storage Buffer、Storage Image、Shader Asset、Readback の実装可否を調査。
- [x] 既存 `PhysicsField`、Thermal、Electromagnetism、Fluid の Registry / Step 順序を記録。

**完了条件**: 既存 Graphics の回帰基準、Compute の不足 API、Backend 制約が文書化される。

#### Phase 00 監査記録（2026-11-19）

GitHub の `master` と独立した検証ツリーの HEAD はともに `b215c151a0543f1abc50dca36eec6c71f6b43ee6`（#387）。RHI に関係する既存差分として、#385 の Alpha 合成設定、#377 の RHI 診断、#364 の Explicit 選択アウトライン、#353 / #354 の補助 Window、#350 の Vulkan 非同期 Picking を確認した。診断や個別 GPU テストの導入と、通常 Application の 3 Backend 回帰確認は別の検証項目として扱う。本流ロードマップ Phase 4 の Smoke Test チェック欄は監査時点で未完了。OpenGL の Dear ImGui 項目は #365 での撤去が反映されていないため、本監査では現行 Raven UI を対象とする。

**ビルドと回帰ベースライン**

- VS 2022 MSBuild / v143、Vulkan SDK 1.4.357.0 で `Root/Root.sln` の Debug / Release x64 を検証した。初回 Debug は checkout に Git 管理外の `GLFW/glfw3native.h` がなく C1083 で失敗。既存の `scripts/bootstrap-windows-build.ps1` が補完するヘッダーと `glfw3.lib` をローカル環境から配置した。
- GLFW 補完後、通常の PDB 構成では両構成に C1902 が発生した。既知の回避手順に合わせて中間出力を分け、コンパイラに `/Z7`、MSBuild に `/p:RavenGenerateLinkDebugInformation=false /p:LinkIncremental=false` を指定し、両構成とも終了コード 0。警告は Debug 33 件（オプション上書き D9025 が 23 件、Astro テストの C4244 が 9 件、CRT の LNK4098 が 1 件）、Release 14 件（D9025）。既定 PDB 構成での成功は未確認。
- Debug の `--test-explicit-frame`、`--test-scene-simulation`、`--test-physics-collision`、`--test-softbody-integrated` は各終了コード 0。`--test-diagnostics` はサンドボックス内で Loopback TCP が拒否されたため、制限外で再実行し 7 suite 成功・失敗 0。

RTX 3080 上で、Debug の通常 `Root.exe` を `Root/Root` から `--backend=<backend>` で起動した。各 Backend で Window 作成、初期 Terrain GLB 読み込み、WindowCloseEvent、終了コード 0 を確認した。

| Backend | 診断と再試行 |
|---|---|
| OpenGL | NVIDIA 610.60、Core Context 3.3.0。初回は起動後約 8 秒で Close を要求し、12 秒以内に終了せず検証プロセスが停止した。起動後 25 秒待つ再試行では正常終了。初回のタイムアウトの原因と GL Debug Callback は未検証。 |
| DirectX 12 | D3D12 Debug Layer と GPU-Based Validation の有効化をログで確認。採取範囲で Validation error は 0 件。Live Object Report の出力は未確認。 |
| Vulkan | 再試行の `VK_LOADER_DEBUG=layer` ログで `VK_LAYER_KHRONOS_validation` の Instance / Device への挿入を確認し、終了コード 0。採取範囲で Validation error は 0 件。初回ログでは Loader が重複する Epic Online Services Overlay Layer を除外した警告が 1 件。 |

この Smoke Test は起動・初期 Scene 読み込み・終了の基準。画面の描画内容、Dynamic Mesh、Physics / Animation Debug 描画、UI 操作、Resize / minimize / restore、Scene 切替は本流ロードマップの残検証項目として扱う。

**Frame と所有権**

- `RHIDevice` は Buffer、Texture、Graphics Pipeline、Offscreen Target の生成境界。`RHISceneCommandList` は Graphics 記録を扱う。Explicit の `IExplicitSceneRuntime` は呼び出し側の Window / Scene を借用し、GPU Resource と準備済み Frame を管理する。Scene 側の Mesh / Material 参照は Runtime の Shutdown 前に解放する。
- Explicit の順序は Prepare → Begin / Acquire → Draw → Submit → Present。Acquire 失敗・Resize・終了時は準備済み Snapshot を破棄する。DX12 は Direct Queue と Frame Fence、Vulkan は Graphics Queue と Frame Slot の Fence / Semaphore を使う。Picking の CPU 参照も対応 Slot の完了後に行う。Compute をこの経路に載せる場合、Render Pass 外の記録位置と Frame 完了までの参照保持を契約化する必要がある。Vulkan は選択 Queue Family の Compute 対応も確認する。
- OpenGL は Window の Context 上で既存 Legacy 描画を実行する。Windows の Context 要求は 3.3。Compute を使う場合は実バージョン・対応機能・GL Loader の確認が必要。

**Compute に必要な RHI 拡張**

| 対象 | 起点と不足項目 |
|---|---|
| Pipeline / Shader | Explicit の `RHIShaderAsset` は Vulkan / DX12 のコンパイル済みバイナリを Path と Entry Point で選ぶ。Compute PSO、Dispatch、Binding Layout、Compute 用の成果物生成と OpenGL の Compute Shader 読み込みが必要。 |
| Storage Buffer | `RHIBufferUsage::Storage` は定義済み。DX12 / Vulkan の Scene Buffer はこの用途を拒否する。OpenGL の既存 Buffer 生成にも SSBO の indexed binding 契約がない。GL の SSBO、DX12 の SRV / UAV、Vulkan の Storage Buffer Descriptor に対応する共通 Binding が必要。 |
| 3D Texture / Storage Image | `RHITextureSpecification` は Width / Height のみで、用途は Sampled / RenderTarget / DepthStencil。Depth、3D View、Storage 用途、書き込み可能な形式（初期 SDF 候補は単精度 Float）、Upload / Readback の行・面 Pitch を定義する。GL は Image Binding、DX12 は 3D UAV、Vulkan は 3D Image / Storage Image と Layout に対応する。 |
| 同期 / Readback | DX12 / Vulkan の Picking は、コピー後に Frame Slot の GPU 完了を待つ実装例。汎用経路には Buffer / Image のコピー範囲、完了状態、Map 可能期間、リソース保持が必要。GL の Memory Barrier、DX12 の Transition / UAV Barrier、Vulkan の stage / access / layout を使い、Compute → Graphics、Compute → Compute、Compute → Readback の依存をそれぞれ定義する。 |

**Physics Registry と Fixed Step**

- `PhysicsField` 系は world-space の CPU 評価契約を持ち、登録は利用ドメインが担当する。永続 `ElectromagneticSystem` は外部 Electric / Magnetic Field を非所有で登録する。呼び出し側は登録中の寿命を維持する。Field 力と Coulomb 力は RigidBody の Force に蓄積され、同じ Fixed Step の Rigid 積分で消費される。
- `FluidWorld` は非所有の Participant と Coupling Binding を保持する。Participant の数値計算後に Static Collider と Dynamic RigidBody の Coupling を解決する。既存 Coupling は CPU Particle 配列を参照する。
- `ThermalWorld` は Field Registry を Step 間で保持する。ECS の Body / Contact 登録は毎 Step 再構築され、Rigid Contact Manifold を追加してから熱計算を進める。TemperatureField の Influence / Blend / Priority はこの経路の既存契約。
- `PhysicsSimulationWorld::StepSimulation` の順序は World Transform 評価 → Astro 重力・天体 Dipole 同期・Electric / Magnetic / Coulomb 外力 → Rigid → Fluid → Soft Collider 同期・Soft Step・反作用 → Transform 再評価 → Thermal 登録と Step。単発 `Step` は最後に `SynchronizeOutputs` を呼ぶ。

### Phase 01 — PhysicsField / Field Storage の責務確定

- [x] 既存 `PhysicsField` / `ScalarField` / `VectorField` API を維持する方針を確定。
- [x] `SignedDistanceField : ScalarField` の CPU 契約を設計（符号・world-space・法線・境界条件）。
- [x] `ScalarGrid3D` / `VectorGrid3D` の CPU 参照ストレージ、補間、Grid Transform を設計。
- [x] GPU Field View / Storage を CPU 仮想 `Evaluate()` と分離。
- [x] CPU/GPU 同期・Readback・更新世代・リソース寿命を定義。
- [x] 電磁場、速度場、温度場の単位とサンプリング規約を整理。
- [x] セル中心・セル面（MAC Grid 等）・ベクトル成分配置の拡張余地を残す。

**完了条件**: 既存 `TemperatureField` に破壊的変更なく、CPU Field と GPU Field を共通の上位概念で扱える設計とテスト方針が確定する。

#### Phase 01 設計記録（2026-11-19）

**Field と Storage の境界**

- CPU は既存 `PhysicsField` タグと、world-space を受け取る `ScalarField::Evaluate` / `VectorField::Evaluate` を維持する。Electric / Magnetic Field は同じ物理量の合成後に力へ変換し、`TemperatureFieldRegistry` は既存の Influence / Blend / Priority で Kelvin 値を合成する。各ドメインの登録 API と登録中の寿命条件を維持する。
- CPU / GPU が共有する上位概念を Field の記述契約とする。値の形（Scalar / Vector）、物理量と単位、world-space の有効領域、Grid を使う場合の変換とサンプル配置を値データとして表す。解析 Field は Grid を持たなくてもよい。CPU Field の既存 API に記述子や Storage を必須化せず、Grid 等で共有が必要な箇所から明示して渡す。ベクトル成分の構成には基底も含める。
- Grid はサンプルを所有して補間する。Field は Grid から物理量として評価し、Solver は Grid を更新する。GPU Field View は同じ記述契約と RHI リソース範囲・更新世代を持つ。GPU Storage はリソースの割り当てと保持を担当する。GPU View に CPU の仮想 `Evaluate()` を持たせず、CPU の利用側は解析 Field、CPU Grid、または明示 Readback で得た CPU スナップショットを選ぶ。

**CPU Signed Distance Field**

- `SignedDistanceField : ScalarField` は world-space の点から表面までの符号付きユークリッド距離をメートルで返す。閉じた体積の内側は負、外側は正、表面はゼロ。開いた形状には符号付き体積の契約を適用せず、必要なら別の Unsigned Distance Field として扱う。離散 Grid の値と補間は距離の近似なので、採用時に解析値との誤差を測る。
- 有限領域では定義域外、無効な入力、欠損サンプルを状態付きの距離照会で失敗として返し、出力値は成功時だけ使用する。継承した `Evaluate()` から無効領域を照会した場合は NaN とする。衝突・境界判定は状態付き照会を使い、未定義領域を表面や外部と誤判定しない。Grid SDF の定義域は有効サンプル位置の範囲に限定し、外挿と端値の延長を行わない。
- 外向き法線は world-space の勾配を単位化して返す。解析勾配を優先し、Grid では各 Grid 軸に沿って 1 サンプル間隔ずつ離れた 2 点の距離を、メートル単位の点間距離で割って中央差分を求める。得られた勾配を world-space へ回転してから正規化する。差分点が有効範囲外に出る場合や勾配がゼロ・非有限の場合は法線照会を失敗とする。球中心など微分不能な点で距離が有効でも法線は無効になり得る。
- 解析形状の剛体変換は距離を維持する。一様スケール `s > 0` では逆変換した点を評価して距離に `s` を掛ける。非一様スケールやせん断でのローカル距離の変換は初期契約の対象外とし、その形状が必要なら world-space 形状から距離を生成し直す。各軸で異なる Grid 間隔はサンプリング解像度を表し、格納済み距離の単位や符号に影響しない。

**CPU 参照 Grid と配置**

- `ScalarGrid3D` は `float`、`VectorGrid3D` の初期形は同一点でサンプルした `math::Vec3` を CPU の連続配列に保持する。Dense 配列は X 最内周で線形添字を `x + Nx * (y + Ny * z)` とする。各軸のセル数と間隔は正、原点は有限、基底は正規直交の回転とする。生成時にサンプル個数とバイト数の積のオーバーフロー、配列長、変換の有効性を検証する。
- 原点 `O` はセル領域の最小側の角、`R` は Grid 基底から world-space への回転、`h` は各軸の間隔。world-space の位置 `p` に対してセル座標は `q = (Rᵀ * (p - O)) / h`（除算は成分ごと）。セル中心の連続サンプル添字は `q - (0.5, 0.5, 0.5)`。初期の Vector Grid は world-space 成分を保持し、位置変換で値の成分を回転・拡縮しない。
- サンプル間は三線形補間する。一般のセル中心 Grid は閉区間 `[0, Nx] × [0, Ny] × [0, Nz]` 内を有効とし、領域内の端の半セルだけ最寄りサンプルへ Clamp する。1 サンプルしかない軸の補間はそのサンプルで一定。領域外、非有限入力、補間に必要な欠損サンプルは失敗として扱う。SDF は上記の厳密な定義域を使う。境界条件としての周期・固定値・流入出は利用側が明示する。
- 変換は領域全体で共有し、サンプル配置は成分ごとにオフセットと有効サンプル数を記述できるようにする。将来の MAC Grid は X/Y/Z 面にそれぞれ `(0, .5, .5)` / `(.5, 0, .5)` / `(.5, .5, 0)` を置き、対応軸だけサンプル数を 1 増やす。各成分を個別に補間後、面法線方向の成分を Grid 基底で組み立てて world-space に回転する。セル中心のベクトル配列と面ごとの配列が同じ物理領域を参照できる。

**物理量とサンプリング**

| Field | SI 単位と値の意味 |
|---|---|
| SDF / 温度 | 距離 `d [m]`、絶対温度 `T [K]`。いずれも world-space の点で評価する Scalar。温度の環境合成は既存 Registry の契約を使う。 |
| 電位 / 電場 | 電位 `V [V]` と world-space の電場 `E [V/m]`。電位から生成する場合は Solver がメートル単位の空間勾配で `E = -∇V` を求める。 |
| 磁束密度 / 電流密度 | world-space の `B [T]` と `J [A/m²]`。Coupling は必要な位置で値を取り、力または熱源へ変換する。 |
| 速度 | world-space の `u [m/s]`。MAC Storage から取得する場合も公開サンプルは world-space ベクトル。 |

新規 Field Storage の境界では座標・距離をメートル、時刻・時間幅を秒とする。別の座標スケールから受け渡す場合はアダプタで単位を変換する。合成規則は物理量と単位が一致する場合に各ドメインが定義する。

**更新世代・Readback・GPU リソース寿命**

1. Solver が Storage の書き込みを予約し、Storage の識別子と単調増加する世代の組で結果を区別する。世代は内容の版として扱い、Frame Slot の番号と独立させる。同一世代のメタデータと内容を GPU 命令の実行中に差し替えない。Storage 再生成時は識別子を更新する。
2. GPU View を次の GPU パスへ渡すときは、記録された Producer と Consumer の依存により、書き込みの完了と可視性を保証する。CPU は GPU パス連携のための待機を挟まない。世代番号だけでは GPU の完了や可視性を保証しない。具体的な Barrier、Queue 操作、完了トークンの RHI 契約は Phase 02 で設計する。
3. CPU 側は View の世代とコピー範囲を指定して Readback を要求する。状態は Pending / Ready / Failed / Cancelled とし、GPU 完了と CPU 可視化を確認してから、世代・有効領域・配置を伴う所有権付きの CPU スナップショットを公開する。部分コピーはコピー済み領域だけを評価可能とする。Pending / Failed / Cancelled では新しいサンプルを公開しない。利用側は要求世代と一致する結果を使い、過去のスナップショットを許す場合は利用側がその世代を明示して選ぶ。
4. 記録から GPU 完了までは実行側が参照リソースを保持する。読み取り中の Allocation を更新で上書きせず、再利用は最後の利用完了後とする。キャンセルや Resize で View やチケットの利用を終了しても、投入済みコピーのソースとステージング領域は完了を観測するまで保持する。CPU スナップショットの寿命は GPU Storage と独立させる。

**実装時のテスト方針**

| 対象 | ケースと合格条件 |
|---|---|
| 既存 CPU Field | `RunPhysicsFieldSelfTests` の ElectricPotential / Temperature と Debug 起動時の Electromagnetism を通す。Temperature Registry の Influence・Blend・Priority・fallback が既存ケースで一致する。 |
| CPU SDF | 半径 2 m の球で内側・表面・外側、中心で法線無効、平行移動・回転・一様拡大を確認する。解析距離誤差は小規模な座標のケースで `1e-5 m` 以下。解析法線の比較は単位ベクトルの内積 `>= 1 - 1e-5`。Grid の離散化誤差はセル間隔ごとに別途計測する。 |
| Grid / MAC | 定数と一次関数を中心・格子間で評価し、回転した Grid と軸ごとに異なる間隔でも期待値に一致する。小規模なデータの補間は各成分の絶対誤差 `1e-5` 以下。1 セル軸、端の半セル、領域外、非有限入力、配列長不一致、積のオーバーフローを確認する。MAC は面ごとのサンプル数・位置と再構成を、対応する Storage 実装時に検証する。 |
| GPU View / Readback | 世代ごとに異なる既知データを書き込み、未完了、逆順完了、部分コピー、キャンセル、再生成を検証する。正しい世代の Ready 領域だけが読め、完了前に Allocation が再利用されず、解放後も取得済み CPU スナップショットが有効であること。数値比較の許容誤差は形式と Backend の検証ケースで定める。 |

Phase 01 の確認は設計と既存コード・セルフテスト入口の照合。上記の新規数値・GPU テストはそれぞれの実装フェーズで実行する。

### Phase 02 — RHI Compute 共通契約

- [x] `RHIComputePipeline` / Specification と Shader 読込契約を設計。
- [x] Compute Resource Binding（Storage Buffer / Image、Uniform、Sampled Texture）を設計。
- [x] Dispatch、リソース遷移・Barrier、可視性、Readback、機能検出を定義。
- [x] Explicit Scene と Legacy OpenGL の責務分離を維持。
- [x] 初期は既存 Graphics Queue / Frame に統合。Async Compute Queue は対象外。
- [x] Render Pass 内外の実行制約、DX12/Vulkan の Descriptor / Root Signature 対応を整理。

**完了条件**: 3 Backend が実装可能な最小共通 API と失敗時契約が定義される。

#### Phase 02 設計記録（2026-11-19）

**Device と記録 API**

| 操作名（設計案） | 入出力と責務 |
|---|---|
| `RHIDevice::QueryComputeCapabilities` | 現在の Device / Context と既存 Graphics Queue での Compute 対応を返す。Workgroup 数・ローカルサイズの各軸上限とローカルサイズの積、Binding の型別上限、Buffer の範囲・オフセット制約、Texture の次元・形式別の Storage 読み書きと Sampling / Copy 可否を返す。機能は用途ごとに判定する。 |
| `RHIDevice::CreateComputePipeline(spec)` | Shader Asset と固定 Binding Layout を検証し、変更不可の `RHIComputePipeline` を生成する。Specification は Backend ごとの Compute 成果物、固定ローカルサイズ XYZ、各 Binding のスロット・型・アクセス・Shader 側対応を持つ。Shader の実際のサイズと Binding はビルド時の検証または Backend の反射情報で照合する。 |
| `RHIComputeCommandEncoder::Dispatch(pipeline, bindings, groups)` | 現在の Frame に 1 回の直接 Dispatch を記録する。groups は XYZ の Workgroup 数。各軸は 1 以上かつ Device 上限以下。要素数からの切り上げと余剰 Invocation の範囲ガードは呼び出し側と Shader が担当する。 |
| `RHIComputeCommandEncoder::Barrier(resource, range, before, after)` | 宣言された Resource 範囲の利用順序と可視性を記録する。利用は段階とアクセスの組で表す。採用する初期用途は Compute Storage、Compute / Graphics Sampled、Uniform、Copy Source / Destination。前の利用状態は初回準備で登録し、その後は Frame の記録状態に照合する。 |
| `RHIComputeCommandEncoder::RequestReadback(resource, region)` | Buffer のバイト範囲または Texture の Mip・領域のコピーを Frame に記録し、非同期チケットを返す。コピー元は Copy Source 利用に遷移済みとする。チケットは Frame Submit の成功時にその完了トークンと結び付ける。 |

Shader Asset は既存の Explicit のバイナリ選択・キャッシュ方針を利用し、Compute 専用段の情報を持つ。初期成果物は OpenGL の GLSL Compute ソース（`main` をコンパイル・リンク）、DX12 の DXIL Compute バイナリ、Vulkan の SPIR-V Compute バイナリと Entry Point。選択 Backend で成果物が欠落・不正・署名不一致の場合は Pipeline 生成を失敗とし、Path・段・Backend を診断する。

**Resource Binding**

- Layout はスロットを一意にし、初期は固定個数の単一 Binding とする。Dispatch ごとに必要な全スロットを指定する。欠落・重複、別 Device のハンドル、範囲・アラインメント、Resource Usage、次元・形式、アクセス指定を記録前に検証する。Backend ごとのスロット対応は Pipeline 生成時に固定し、Graphics Pipeline の Binding Layout と独立させる。
- Storage Buffer はバイト範囲と要素 Stride を持つ。Shader 側の要素レイアウトと合わせる。Storage Image は Mip 1 段、次元、形式とアクセスを指定する。初期の 3D Storage View は対象 Mip の全 Depth を含む。Storage のアクセスは Read / Write / ReadWrite で宣言し、確保時に必要な用途（例として Storage + Sampled + Copy Source）を指定する。
- Uniform Buffer は読み取り専用のバイト範囲、Sampled Texture は読み取り専用の Texture View と Sampler を指定する。初期 Sampler は Point / Clamp を共通対応とし、補間機能や形式との組み合わせを追加するときは機能を検査する。Uniform と Storage Buffer のデータレイアウトは Shader 成果物と CPU 側のメタデータで照合し、C++ 型の暗黙のパディングに依存しない。
- 同一 Dispatch で重複する範囲を複数スロットに割り当てる場合は、すべてが読み取り専用で、同じ Resource 状態と Layout で実行できるときに限り許可する。読み書きが必要な場合は単一 ReadWrite Binding を使い、Invocation 間の競合回避を Shader 側で保証する。Backend が範囲別の状態を表現できない場合は、初期実装で Resource 全体を保守的に同期する。

**Frame と同期**

1. Resource / Pipeline / Binding の準備を `PrepareFrame` 側で行う。Explicit は `BeginFrame` 成功後、最初の Graphics Render Pass を開始する前のアクティブな記録範囲で Compute を実行する。Compute の Dispatch・Barrier・コピーは Render Pass の外で行う。初期経路は同じフレーム内の Compute → Graphics を支援し、Graphics の結果が入力となる Compute は先行フレームの結果を利用する。
2. Explicit は Window / Scene の既存所有関係を維持し、DX12 の Direct Queue と Vulkan の Graphics Queue 上に専用の Compute Encoder を接続する。OpenGL は現行 Window の Context を所有するスレッドで、Graphics 描画開始前に同じ契約を実行する。GL の状態キャッシュは描画側と整合させる。Async Compute、別 Queue への所有権移譲、間接 Dispatch は後続課題。
3. Write → Read、Write → Write、Read → Write の依存、および利用状態・Image Layout の変更を Barrier で宣言する。Write → Read の可視性は後続のアクセス種別で決める。Compute → Compute は Storage、Compute → Graphics は最初の Sampled Shader 段、Compute → Readback は Copy Source への遷移を指定する。同じ状態に留まる Storage でも重複範囲に書き込み依存があれば順序を付ける。GPU 内の同期と CPU からの完了確認は別の操作とする。
4. Compute は既存 Frame とともに Submit / Present する。Acquire / Begin が失敗した場合はその Frame の Compute を実行せず、準備データを破棄する。コマンド記録時点から完了まで Pipeline、Binding に使った Resource と Descriptor、コピー先ステージングを保持する。再利用の条件は該当 Submit の GPU 完了とする。

**Readback と失敗時契約**

- 完了トークンは Submit を一意に識別する。Frame Slot の番号や Field の更新世代とは独立させる。Readback は Pending から Ready / Failed / Cancelled に遷移する。Resize やキャンセルでも、投入済みコピーが参照する Resource の寿命は GPU 完了まで保持する。Device Lost 時はチケットを Failed にし、Device の破棄経路で所有リソースを処理する。
- チケットをポーリングし、GPU 完了と CPU 可視化を確認した後に限り、所有権付きの CPU データを返す。結果は入力範囲、形式、行 Pitch・面 Pitch（Texture の場合）を伴う。Backend のステージングレイアウトを共通の結果に変換するときも、これらの値を明示する。Field の呼び出し側は Phase 01 の Storage ID / 世代とチケットを対応付ける。
- 共通結果は成功、未対応、無効な引数・状態、リソース確保失敗、Backend 失敗、Device Lost を区別する。Compute を使う Application はCapability 不足時の扱いを決める。引数・機能・記録位置のエラーはネイティブ命令を記録する前に返し、当該操作による記録状態の更新を行わない。ネイティブ記録や Submit が失敗した場合は既存 Frame の失敗経路へ渡し、完了が保証できないコピーのチケットを Ready にしない。OpenGL の実行済み命令については巻き戻しを仮定せず、失敗した出力を後続パスに公開しない。

**Backend 対応**

| Backend | Pipeline / Binding | 同期 / Readback と初期制約 |
|---|---|---|
| OpenGL | GL 4.3 Core を初期目標とし、Context 初期化後にバージョン・GL Loader の必須関数・実機上限を調べる。GLSL Compute Program に UBO / SSBO の indexed binding、Texture / Sampler unit、Image unit を割り当てる。 | Dispatch は Context 上で順次発行する。Storage、Texture Fetch、コピー等の後続用途に応じて `glMemoryBarrier` のビットを選び、コピー後の Sync Object 完了を CPU で確認する。Phase 00 の GL 3.3 Context では Compute capability を未対応とする。 |
| DirectX 12 | Compute PSO と Compute Root Signature を使用する。Layout から Descriptor Table を生成し、Uniform を CBV、Sampled を SRV + Sampler、Storage を UAV に割り当てる。Storage Image の読み取りは対象形式で Typed UAV Load が可能な場合に公開する。 | Direct Queue の既存 Command List に記録する。用途変更は Resource Transition、同一 UAV 状態の依存は UAV Barrier で表現する。Readback Heap へのコピー（Texture は copy footprint を使用）を Frame Fence の完了後に読む。 |
| Vulkan | Graphics Queue Family が `VK_QUEUE_COMPUTE_BIT` も持つことを確認する。Compute Pipeline、Set 0 の固定 Descriptor Set Layout、Uniform Buffer / Storage Buffer / Storage Image / Combined Image Sampler を使用する。 | Render Pass 外で記録する。Storage Image は `GENERAL` を使い、Sampling と Copy には対応する Layout に遷移する。Pipeline Barrier で stage / access / layout を指定する。Staging の Host 読み取りは Submit Fence 後に行い、非 Coherent メモリなら必要な範囲を Invalidate する。 |

Backend の対応確認に用いた仕様資料は [OpenGL 4.3 Reference Card](https://www.khronos.org/files/opengl43-quick-reference-card.pdf)、[OpenGL Memory Model](https://wikis.khronos.org/opengl/Memory_Model)、[DX12 Root Signature](https://learn.microsoft.com/en-us/windows/win32/direct3d12/creating-a-root-signature)、[DX12 Resource / UAV Barrier](https://learn.microsoft.com/en-us/windows/win32/direct3d12/dml-barriers)、[Vulkan Specification](https://registry.khronos.org/vulkan/specs/latest/html/vkspec.html)。

**実装時のテスト方針**

- 共通契約の負例は Unsupported Context / Queue、Shader または Binding の不一致、Layout 上限、範囲外の Dispatch とコピー、Render Pass 内の Compute を含む。各失敗が事前検証で返り、有効な Graphics フレームが継続できることを確認する。
- Phase 03 / 04 では整数配列の既知演算を使い、複数 Workgroup と端数要素を含む出力が CPU 参照と完全一致することを確認する。Compute → Compute の 2 段処理、Compute → Graphics の 3D Texture Sampling、Compute → Readback の Buffer / Texture を個別に検証する。Readback は完了前にデータを取得できず、完了後に値と Pitch が一致することを確認する。
- Acquire 失敗、Resize、キャンセル、Shutdown 時に未完了リソースを使い回さないことを検証する。OpenGL 3.3 の既存 Graphics、DX12 Debug Layer、Vulkan Validation Layer、通常 Application の描画・終了も実装後に確認する。Phase 02 の検証は既存コードと仕様への設計照合までとする。

### Phase 03 — OpenGL Compute Smoke Test

- [x] Compute Shader コンパイル / リンク、Storage Buffer Binding、Dispatch を実装。
- [x] Memory Barrier と GPU→CPU Readback を実装。
- [x] 既知の配列演算を CPU 参照値と比較。
- [x] 3D Storage Texture 書込と描画側 Sampling の依存関係を確認。
- [x] 既存 OpenGL Scene / UI / Resize の回帰を確認。

**完了条件**: GPU計算結果を CPU と比較でき、Graphics への受け渡しが正しく動く。

#### Phase 03 実装・検証記録（2026-11-19）

- `RHIDevice` から Compute の能力を照会し、対応 Context では単一 Context の Compute Encoder を取得できる。GL 4.3 Core と必須の GL 関数を確認する。既存 Application は従来の 3.3 Context を使い、Compute 検証モードが 4.3 Context を明示的に作る。
- Encoder は GLSL のコンパイル・リンク、Workgroup サイズと有効な Storage Binding の照合、SSBO と単一 Mip の R32F 3D Image の生成・Dispatch を担当する。Dispatch の後は、呼び出し側が次の Storage、Texture Sampling、Buffer コピーの用途を指定して Barrier を発行する。Graphics 側は同じ Context から 3D Texture を Sample できる。
- Buffer Readback は指定範囲をステージング Buffer にコピーし、GL Sync の完了を待たずにポーリングする。完了時に CPU 所有のバイト列を返す。Encoder とハンドルはその Context がカレントの間に使用し、破棄前に解放する。初期範囲は全体 SSBO Binding、R32F 3D Image、Buffer Readback。Texture Readback や Compute 内の Uniform / Sampled Texture Binding は残課題。
- RTX 3080、NVIDIA 610.60、OpenGL 4.3 Core で 19 要素の整数配列を 2 回 Dispatch し、Readback が CPU 参照値と完全一致した。Fragment Shader で 2×2×2 Texture の全 8 値をサンプリングし、Framebuffer の R 値が 8-bit 換算で 1 以内に一致した。ゼロ Workgroup と範囲外 Readback も拒否した。
- 通常の OpenGL 3.3 Application で Scene と Raven UI を目視し、最大化 3840×2054 と復元 1920×1080 の両 Resize 後に描画を確認した。Window Close の終了コードは 0。
- Debug / Release x64 の通常 Application と Debug x64 の GPU 統合テストをビルドし、いずれも終了コード 0。Phase 00 と同じ GLFW 依存と PDB 回避設定を使用した。

GPU テストは `RavenUIGPUTest=true` の Debug x64 ビルド後、`Root/Root` から `../x64/Debug/RavenUIGPUTests.exe --opengl-compute` で実行する。4.3 Context を利用できない場合はテストが失敗として報告する。

### Phase 04 — DirectX 12 / Vulkan Compute

- [x] DX12 Compute PSO、Root Signature、SRV/UAV、Dispatch、UAV/Resource Barrier。
- [x] Vulkan Compute Pipeline、Descriptor、`vkCmdDispatch`、Image Layout / Pipeline Barrier。
- [x] GPU Resource lifetime、Frame内依存、Submit / Present を検証。
- [x] 3 Backend で同一テストデータの結果を許容誤差内で比較。
- [x] DX12 Debug Layer、Vulkan Validation Layer、Resize / Scene 切替を検証。

**完了条件**: 3 Backend で Compute + Graphics の Smoke Test が成功し、重大な Validation error がない。

#### Phase 04 実装・検証記録（2026-11-19）

- DX12 は DXIL、Compute PSO、Compute Root Signature と UAV Descriptor Table を使う。初期データは Upload Heap から Storage Buffer に転送する。Compute 同士は UAV Barrier、Graphics への受け渡しは Texture の Shader Resource 遷移、Readback は Copy Source 遷移で同期する。
- Vulkan は既存 Graphics Queue Family の Compute 対応を確認し、SPIR-V、Compute Pipeline、Storage Buffer / Image Descriptor を使う。3D Image は `GENERAL` と `SHADER_READ_ONLY_OPTIMAL` を使い分け、Readback の転送と CPU 参照を Barrier で同期する。SwapChain Image の Layout 遷移は Acquire Semaphore の待機 Stage に接続する。
- 両 Backend とも `QueueFrame` で描画前の Compute を予約する。既存 Frame が GPU 完了を観測するまで、Pipeline、Descriptor、Storage、Readback 先を保持する。Buffer Readback は完了確認後に CPU 所有のコピーを返す。初期対象は全体 Storage Buffer Binding、単一 Mip R32F 3D Image、Buffer Readback。Smoke Test の Graphics は Backend の Sampled View / SRV を使う。
- RTX 3080 で 3 Backend とも整数 19 要素が CPU 参照値と完全一致し、3D Texture の全 8 ボクセルを Fragment / Pixel Shader で読み、Framebuffer の R 値が 8-bit 換算で 1 以内に一致した。DX12 / Vulkan は 2 つの Compute Frame で Storage を再利用し、各 Frame の Present 後の SceneManager による Primary Scene 交換、Resize 後の Submit / Present、フェンス完了後の Readback を検証した。
- DX12 Debug Layer と GPU-Based Validation、Vulkan Khronos Validation と同期検証を有効にし、最終 GPU テストの Validation error は 0 件。Vulkan の Loader には重複する Epic Online Services Overlay Layer の警告が 1 件あった。通常の DX12 / Vulkan Application でも、実ウィンドウの最大化による Resize 後に Scene と Raven UI が描画され、正常終了することを確認した。
- Debug / Release x64 と Debug x64 GPU 統合テストをビルドし、既存 Explicit Frame / Scene Lifecycle セルフテストも成功した。Phase 00 と同じ GLFW 依存の補完と PDB 回避設定を使用した。

検証用 Shader をリポジトリルートから Vulkan SDK の `glslc` / `dxc` で生成する。

```powershell
$out = New-Item -ItemType Directory -Force Root/Tests/x64/ComputeShaders
foreach ($stage in @('comp', 'vert', 'frag')) {
    & "$env:VULKAN_SDK/Bin/glslc.exe" "Root/Tests/ComputeShaders/Smoke.$stage" -o "$out/Smoke.$stage.spv"
    if ($LASTEXITCODE -ne 0) { throw "glslc failed ($stage)" }
}
foreach ($stage in @('cs', 'vs', 'ps')) {
    & "$env:VULKAN_SDK/Bin/dxc.exe" -T "${stage}_6_0" -E "$($stage.ToUpperInvariant())Main" Root/Tests/ComputeShaders/Smoke.hlsl -Fo "$out/Smoke.$stage.dxil"
    if ($LASTEXITCODE -ne 0) { throw "dxc failed ($stage)" }
}
```

`RavenUIGPUTest=true` の Debug x64 ビルド後、`Root/Root` から `../x64/Debug/RavenUIGPUTests.exe` を `--opengl-compute`、`--dx12-compute`、`--vulkan-compute` で実行する。DX12 は `RAVEN_RHI_VALIDATION=1` と `RAVEN_DX12_GPU_VALIDATION=1`、Vulkan は `RAVEN_RHI_VALIDATION=1` と `VK_LAYER_VALIDATE_SYNC=1` を設定する。

### Phase 05 — GPU Dense SDF

- [x] 解析的球 SDF を GPU 3D Texture に生成し CPU 参照と比較。
- [x] Triangle Mesh → Unsigned Distance Field（Voxel-Centric）を実装。
- [x] 閉じたメッシュの内外符号判定を実装。開いたメッシュは別契約にする。
- [x] 境界法線、薄い形状、退化三角形、Transform、Voxel解像度を検証。
- [x] SDF を可視化し、CPU/GPU 距離誤差を測定。

**完了条件**: 既知形状とメッシュで符号・距離・法線の誤差が規定値内に収まる。

#### Phase 05 実装・検証記録（2026-11-19）

- `DenseSDFSource` は CPU 上で球または world-space のインデックス付き三角形 Mesh を評価する。符号付き Mesh は各有向辺が逆向きの辺と 1 組になり、面が非退化であることを検査する。頂点の溶接、自己交差の回避、体積境界としての向きの整合は入力の前提とする。閉じた Mesh の符号は立体角の総和から求め、距離の大きさは各三角形への最短距離とする。開いた Mesh は符号なしを指定し、退化面は線分と点として評価する。[Generalized Winding Numbers](https://igl.ethz.ch/projects/winding-number/)
- `DenseSDFGrid` は world-space に軸を揃えたセル中心 Grid。軸ごとの間隔はメートル。法線は近傍の距離を中央差分し、単位化する。境界セルやゼロに近い勾配は法線なし。Mesh の Transform は頂点へ適用してから入力する。
- `DenseSDFGenerator::Prepare` で Pipeline と Resource を作り、`Record` が Compute と Barrier、非同期 Readback を記録する。OpenGL では同一 Context、DX12 / Vulkan では `QueueFrame` 経由で記録する。Shader は各ボクセルで評価した距離を単一 Mip の R32F 3D Texture と数値検証用 Buffer に書く。測定と断面表示は Buffer の値を使用し、Texture 自体の Readback は扱わない。閉じた Mesh の計算量はボクセル数 × 三角形数。Sparse 化と高速化は Phase 06 で扱う。
- RTX 3080 上の各 Backend で、球 2 解像度、平行移動・回転・軸ごとの拡縮を適用した箱、厚さ 0.13 m の薄い箱、開いた平面、退化線分を比較した。合格条件は距離の最大絶対誤差 `1e-4 m` 以下、CPU 参照の格子法線との内積 `0.999` 以上、球の解析法線との内積 `0.998` 以上。境界セルの法線なしと、開いた Mesh / 退化面の符号付き入力拒否も確認した。
- 3 Backend で各 89,812 ボクセルを評価した。CPU 実装と GPU の距離差、および解析距離に対する GPU の誤差は、いずれも最大絶対値 `3.58e-7 m`。球の解析法線との最小内積は粗い Grid で `0.999964`、細かい Grid で `0.999997`。DX12 GPU-Based Validation と Vulkan 同期検証を有効にしたログの Validation error は 0 件。各 Backend で 6 枚、計 18 枚の断面 SVG を生成した。
- Debug / Release x64 と Debug x64 GPU 統合テストは成功。Phase 00 と同じ GLFW 依存と PDB 回避設定を使用した。既存の 3 Backend Compute → Graphics と、Explicit Frame / Scene Lifecycle セルフテストも成功した。

リポジトリルートから検証用バイナリ Shader を生成する。

```powershell
$out = New-Item -ItemType Directory -Force Root/Tests/x64/ComputeShaders
$shader = 'Root/Root/Raven/Assets/Shaders'
& "$env:VULKAN_SDK/Bin/glslc.exe" -DRAVEN_VULKAN=1 "$shader/DenseSDF.comp" -o "$out/DenseSDF.spv"
if ($LASTEXITCODE -ne 0) { throw 'Dense SDF SPIR-V compilation failed' }
& "$env:VULKAN_SDK/Bin/dxc.exe" -T cs_6_0 -E CSMain "$shader/DenseSDF.hlsl" -Fo "$out/DenseSDF.dxil"
if ($LASTEXITCODE -ne 0) { throw 'Dense SDF DXIL compilation failed' }
```

`RavenUIGPUTest=true` の Debug x64 ビルド後、`Root/Root` から `../x64/Debug/RavenUIGPUTests.exe --dense-sdf=opengl` を実行する。DX12 / Vulkan は引数を `--dense-sdf=dx12` / `--dense-sdf=vulkan` に替え、Phase 04 と同じ Validation 設定を使う。各ケースの XY 中央断面が `Root/Tests/x64/ComputeShaders/DenseSDF-<backend>-<case>.svg` に出力される。青は内側、赤は外側、白は表面近傍。

### Phase 06 — Narrow-Band / Sparse SDF

- [x] Triangle-Centric 候補範囲とセル単位の最小距離集約を検証。
- [x] Narrow Band、Sparse Tile、Tile Lookup、タイル境界補間を実装。
- [x] 距離の量子化（8-bit 等）は精度測定後に採否を決定。
- [x] Incremental Update / フレーム分散を実装。
- [x] Dense SDF に対してメモリ・GPU時間・距離精度を比較。

**完了条件**: 精度劣化を定量化したうえで、対象シーンに対する省メモリまたは高速化が確認できる。

#### Phase 06 実装・検証記録（2026-11-20）

- 各三角形の world-space AABB を補間の余白を含む帯域で広げ、重なるタイルへ候補を登録する。GPU は 4×4×4 セルを 1 Workgroup で評価し、各セルの 1 Invocation がそのタイルの候補の最小距離を集約する。セルごとに書き込み元が 1 つなので、Atomic Min 用の初期化・競合を避けられる。符号付き Mesh は、帯域に届いたセルでのみ全三角形の立体角を評価する。
- `SparseSDFSnapshot` はタイル座標で検索できる CPU スナップショット。GPU バッチは Storage Buffer にタイル順で出力し、Readback の完了後に CPU 側へ保持する。公開帯域幅にセル対角長を加えた距離まで補間サンプルを残す。`TrySample` は必要な隣接タイルを横断して三線形補間し、帯域外や欠損は失敗として返す。
- `SparseSDFUpdater` は `BeginUpdate` → `PrepareBatch` → `RecordBatch` → `Poll` で反復する。OpenGL は同一 Context、DX12 / Vulkan は `QueueFrame` を使う。1 回の更新で同じ Encoder を使い、すべてのバッチの Readback が完了した時点で新しい世代を公開する。未変更タイルのサンプルは前の世代と共有する。
- 符号なし Mesh は候補の集合と対応する三角形座標が同じタイルを再利用する。符号付き Mesh は同じインデックス接続の頂点移動に対し、変更面の始点・終点を含む掃引 AABB を帯域分拡張し、その周辺の現行タイルを再生成する。接続が変わる場合は現行候補全体を再生成する。更新で消えたタイルは次の公開世代から除かれる。
- メモリ比較には Dense の R32F サンプル配列と Sparse のタイルキー + R32F サンプルの論理バイト数を使う。Sparse のピーク GPU ペイロードは 1 バッチの入力・出力 Buffer の論理サイズを別途記録する。コンテナ・アロケータ・ステージングの実割当は含めない。GPU 時間は Compute 記録と GPU コピーを挟む各 Backend の GPU タイムスタンプを使い、バッチごとの区間を合計する。

RTX 3080、65×61×57 セル、間隔 0.10 m、公開帯域 ±0.13 m で比較した。Dense のサンプルは各ケース 904,020 バイト。Sparse は球で 34,572 バイト（96.2% 減、129 タイル）、閉じた箱 2 個で 53,600 バイト（94.1% 減、200 タイル）。Sparse のピーク GPU 入出力ペイロードは球 18,512 バイト、箱 26,960 バイト。

| Backend | 球 Dense → Sparse（ms） | 箱 Dense → Sparse（ms） |
|---|---:|---:|
| OpenGL | 5.393 → 2.649 | 4.720 → 60.200 |
| DX12 | 0.110 → 0.052 | 0.238 → 0.113 |
| Vulkan | 0.365 → 0.030 | 0.419 → 0.110 |

GPU 時間は各 3 回の中央値。OpenGL の Sparse 箱は 42.7〜60.5 ms で Dense の 4.0〜9.4 ms より遅く、性能改善は追加調査が必要。

- 3 Backend とも Dense に対する帯域内のセル距離差は 0 m。タイルをまたぐ補間の最大絶対誤差は `2.24e-7 m`（合格基準 `1e-4 m`）。箱の片方を移動した差分更新は、符号付き・符号なしとも 225 タイル中 125 タイルを 8 バッチで再生成し、全再生成と許容誤差内で一致した。最終バッチまで旧世代を返し、準備後のキャンセルでも公開世代を維持した。
- 補間の余白を含む範囲を対称な 8-bit 符号付き整数に量子化する試算では、最大絶対誤差 `1.19e-3 m` が基準 `1e-4 m` を超えたため、R32F を採用した。
- Debug / Release x64、Debug x64 GPU 統合テスト、従来の 3 Backend Dense SDF と Explicit Frame / Scene Lifecycle テストはすべて成功。DX12 GPU-Based Validation と Vulkan 同期検証のエラーは 0 件。ビルドには GLFW 依存の補完と既存 PDB 回避設定を用いた。

検証用 Shader は Phase 05 と同じ出力先に `DenseSDF` と `SparseSDF` の両方を生成する。

```powershell
$out = New-Item -ItemType Directory -Force Root/Tests/x64/ComputeShaders
$shader = 'Root/Root/Raven/Assets/Shaders'
foreach ($name in @('DenseSDF', 'SparseSDF')) {
    & "$env:VULKAN_SDK/Bin/glslc.exe" -DRAVEN_VULKAN=1 "$shader/$name.comp" -o "$out/$name.spv"
    if ($LASTEXITCODE -ne 0) { throw "SPIR-V compilation failed ($name)" }
    & "$env:VULKAN_SDK/Bin/dxc.exe" -T cs_6_0 -E CSMain "$shader/$name.hlsl" -Fo "$out/$name.dxil"
    if ($LASTEXITCODE -ne 0) { throw "DXIL compilation failed ($name)" }
}
```

`RavenUIGPUTest=true` の Debug x64 ビルド後、`Root/Root` から `../x64/Debug/RavenUIGPUTests.exe --sparse-sdf=opengl` を実行する。DX12 / Vulkan は `--sparse-sdf=dx12` / `--sparse-sdf=vulkan` とし、Phase 04 と同じ Validation 設定を使う。

### Phase 07 — Physics / Fluid / Particle 連携

- [x] SDF を静的境界として使う Collision / Contact を追加（既存 Sphere / Box を維持）。
- [x] `FluidWorld` と既存 CPU SPH を維持し、SDF 境界連携を追加。
- [x] GPU Grid Fluid は別 Solver / Participant として導入。
- [x] GPU 粒子と速度場のサンプリング・衝突を追加。
- [x] CPU `std::vector<FluidParticle>` 前提の Coupling と GPU 状態を暗黙に混在させない。
- [x] 同期タイミング、固定 Step、決定性、GPU Readback 頻度を検証。

**完了条件**: 既存 CPU Fluid の回帰がなく、GPU SDF 境界を用いた新規デモが動く。

#### Phase 07 実装・検証記録（2026-11-20）

- `StaticSDFColliderComponent` は world-space の符号付き CPU Field または公開済み Sparse スナップショットを所有する。球形剛体は距離と法線から既存 `ContactManifold` を作り、CPU SPH は同じ球・SDF 接触を既存 Static Collider Coupling で使う。帯域外・欠損・法線不能は接触照会失敗。剛体側の初期対応は離散ステップの動的 Sphere。
- `GPUGridFluid` は独立した `FluidSimulationParticipant`。速度場を半ラグランジュ移流・拡散・外力で更新し、粒子は更新後の速度場を三線形サンプリングする。初期実装は圧力射影なし。粒子状態と速度場は GPU Buffer に保持され、CPU SPH の Coupling Binding には登録しない。
- GPU は同じ Dense SDF Texture を接触と描画に利用する。GPU 粒子は 1 サブステップあたり最小セル間隔の 1/4、最大 32 回に移動を制限して接触を調べる。法線・補正が成立しない場合は位置を保持し、診断フラグを残す。薄い形状が Grid に記録されることが前提で、連続形状への一般的な CCD は別課題。
- フレームでは固定ステップの速度更新 → Barrier → 粒子更新 → Barrier → 描画の順に実行する。Dense SDF の生成と通常描画は GPU 上で完結する。数値テストが診断位置を要求した場合にだけ、粒子 Readback のチケットを作る。
- RTX 3080 の OpenGL 4.3 で、45 粒子を 1/60 秒で 90 ステップ進める試行を 2 回実行し、同一 GPU で結果がビット単位で一致した。各試行の接触は 2,061 回、最終時点の解析球に対する最大貫通は `0.002398 m`（基準 `0.012 m`）。ステップ中の Readback は 0 回、診断時は試行ごとに 1 回。異なる GPU や Backend 間での決定性は未検証。
- 高速粒子は 1 ステップの移動を 0.80 m に制限し、厚さ 0.06 m の壁に接触した時点で壁の手前（X=-0.045 m）に停止した。球中心の法線が未定義のケースでは、位置を維持し診断フラグを確認した。専用ウィンドウで SDF 断面と粒子の描画、正常終了を目視確認した。
- Debug / Release x64 と Debug GPU 統合テストをビルドし、Debug / Release の OpenGL Fluid 統合テスト、CPU Collision / SPH / Scene Simulation / Explicit Frame が成功。3 Backend の Dense / Sparse SDF 回帰で、公開済み符号付き Sparse からの CPU 接触、帯域外での照会失敗、符号なしスナップショットの拒否を確認した。DX12 / Vulkan の Validation error は 0 件。GLFW 補完と既存 PDB 回避設定を用いた。

`Root/Root` から `../x64/Debug/Root.exe --gpu-sdf-fluid-demo` を実行すると、OpenGL 4.3 の専用ウィンドウで球の SDF 断面と粒子を描画する。Esc で終了する。統合テスト入口は `--test-gpu-sdf-fluid`、CPU 回帰入口は `--test-physics-collision` と `--test-fluid-sph`。

### Phase 08 — Electromagnetism Field / Solver 連携

- [x] SDF + Material ID から `σ/μ/ε` 等の Material Field を構築。
- [x] 電位 Scalar Field と静電・定常電流 Solver を追加。
- [x] 電場 `E=-∇V`、電流密度 `J=σE` を Vector Field として公開。
- [x] 導体/絶縁体境界の条件、セル面係数、電流保存を検証。
- [x] 磁気静力学 Solver と磁束密度 Field を段階追加。
- [x] ローレンツ力連携とジュール熱源 `J·E` を追加。
- [x] 解析解・電荷保存・電流保存・境界条件の誤差を検証。

**完了条件**: 既知の電極/導体/コイルの検証ケースで Field と Coupling が整合する。高周波 Maxwell / 渦電流 / 非線形磁性は別計画とする。

#### Phase 08 実装・検証記録

- `ElectromagneticMaterialGrid::Build` は world-space のセル中心を符号付き SDF で分類する。重複領域では入力順を優先し、領域の外側は背景 Material ID。構築後の Solver は ID と材料係数のみを保有する。
- 静電の `−∇·(ε∇V)=ρ` と定常電流の `∇·(σ∇V)=0` は同じセル中心有限体積 Stencil を使う。固定電位は電極セルの中心に指定する。外周は零流束、定常電流では導電率 0 の面を絶縁として扱う。面係数は隣接セル係数の調和平均で、面積と中心間距離を反映する。保存則はセル中心の補間値ではなく、同一の面積分流束を逆符号で共有して評価する。
- 固定電位に接続しない有効成分は `MissingReference`、Gauss–Seidel が残差許容値に届かない場合は `IterationLimit` を返す。収束結果は共有の読み取り専用スナップショットで、各 Field はセル内一定の値を評価する。定常電流で計算対象外の絶縁セルと、格子範囲外の評価値は 0。既存の電磁場 Registry に登録する Field 本体は呼び出し側が所有し、登録解除または World の破棄まで生存させる。
- `CoilMagneticField` は均一透磁率の無限領域で、閉じた折れ線コイルの既知の定常電流を Biot–Savart 則で積分する。各線分は有限コアを用いた解析積分。材料ごとに透磁率が異なる境界での磁場や、定常電流格子を磁場のソースへ変換する処理は今後の課題。格子のジュール発熱密度 `J·E` は Phase 09 で熱輸送の入力に使う。
- `Root.exe --test-electromagnetism` で、平行平板電極の電位、直列導体の電流と絶縁面の零流束、電荷収支、コイル軸上の磁束密度、Scene fixed-step のローレンツ力と発熱密度を確認する。許容値は電流と電荷の収支で `1e-8 A / C`、コイル軸上の磁束密度で相対 `1e-3`。

- 数値検証では面電流の解析値との差が最大 `3.89e-11 A`、自由セルの電流収支誤差が最大 `1.78e-11 A`。代表セルの電荷収支誤差は `1.40e-11 C`、円形コイル軸上の磁束密度の相対誤差は `7.91e-5`。

### Phase 09 — Thermal / Multiphysics 連携

- [x] 既存 `TemperatureField` / `ThermalWorld` を維持。
- [x] Grid Temperature Field と熱輸送・拡散を段階追加。
- [x] 電磁気のジュール熱、流体移流、粒子熱源の Coupling を追加。
- [x] 燃焼・着火・延焼は独立したモデルとして段階追加。
- [x] 保存則、安定性、Step順序、CPU/GPU同期を検証。

**完了条件**: 電流→発熱→温度上昇、および流体→熱移流の独立テストが通る。

#### Phase 09 実装・検証記録

- `GridTemperatureField` は一様の容積熱容量・熱伝導率を持つセル中心格子。断熱・不透過の外周とし、熱伝導と風上移流を同じ面の両側へ逆符号で反映する。1 Step に各セルから出る係数の総和が 1 を超える場合は温度更新を拒否する。温度 Field の領域外 Influence は 0。
- `GridThermalCoupling` は Phase 08 の同一格子スナップショットから `J·E [W/m³]` を取り込み、粒子熱源 `[W]` を含有セルへ加える。格子は Fluid 更新後、既存 Thermal の環境接触構築前に Step する。既存の集中熱容量 Body への境界温度は一方向で提供する。格子の熱収支には Body からの反作用を含めない。
- CPU ではセル面の速度を `VectorField` から評価する。OpenGL GPU 流体では同一格子のセル中心速度を非同期で取得し、隣接中心の平均を面速度にする。BufferReadback の Barrier とコピーを記録し、未完了なら直前の完了世代を再利用する。最初の完了データが届くまでは格子の Step を保留する。GPU 連携を設定した場合にだけ、最大 1 件の未完了チケットを保持する。値の鮮度は利用世代と Fluid の進行世代で確認できる。
- 簡易反応モデルは燃料密度、着火温度、消費速度、反応熱から Step の候補を計算する。格子更新の成功時に燃料を確定し、熱拡散で温度が上昇した隣接セルは次 Step から着火できる。化学反応・酸素供給・炎の流体力学は扱わない。
- `Root.exe --test-thermal-grid` で定常電流からの温度上昇、粒子熱源、同じ Step の環境温度、CPU 熱移流、熱量保存、過大 Step の拒否と燃料ロールバック、隣接セルの着火を確認する。`Root.exe --test-gpu-sdf-fluid` で OpenGL の速度世代、初回取得時間、追加 Readback 回数、熱移流と収支を確認する。

- CPU テストでは中央セルが `300 K → 301.5 K` に上昇し、組み合わせた熱源の収支誤差は `0 J`。独立した移流ケースでは下流へ `25 J` 移動し、隣接セルへの着火は熱拡散の次 Step から発生した。OpenGL の Release 検証では Step 2 が完了済み世代 1 を使用し、追加の速度 Readback は 2 件、粒子 Readback は 0 件。下流セルの温度上昇は `20 K`、収支誤差は `0 J`。初回固定 Step の CPU 側所要時間は約 `1.2 ms`、速度スナップショットの初回観測は約 `14.0 ms`（テストのポーリング間隔を含む）。

### Phase 10 — 計測・最適化・正式統合

- [x] GPU Timestamp、Dispatch数、メモリ使用量、Readback待機を計測。
- [x] Descriptor / Pipeline / Buffer 再利用、Sparse更新を最適化。
- [x] 3 Backend、既存 Scene、Physics / Thermal / UI の回帰テスト。
- [x] Debug / Release ビルドと GPU Validation を検証。
- [ ] 長時間動作の検証（今回の対象外）。
- [x] API使用例、デモ、トラブルシュート、測定結果を文書化。

**完了条件**: 正確性・性能・リソース寿命の基準を満たし、既存機能の重大な回帰がない。


#### Phase 10 の計測と再利用

- `RHIComputeCommandEncoder::MeasureGpuWork` は記録する処理を 1 回呼び出し、GPU Timestamp の Ticket を返す。OpenGL では同じ Context で Query の利用可否を Poll する。DirectX 12 / Vulkan は Explicit Frame の Fence 完了後に結果を取り出す。非対応の場合は処理の記録を続け、Ticket は空になる。
- Timestamp の区間は指定した Compute 記録。SDF 計測は Dispatch・Barrier・Readback Copy を含む。ホスト側の Readback 観測時間は Copy の要求から Ready を観測するまでで、Explicit Backend では Frame 提出・完了通知、OpenGL では Poll の間隔も含む。GPU 計測値と独立に記録する。
- `GetStatistics()` は Encoder ごとの累積カウンター。Buffer / R32F Texture / Readback の Bytes はそれぞれの論理ペイロード量。ドライバのアラインメント、配置、常駐量、Upload Buffer、Timestamp の内部資源は対象外。
- DirectX 12 / Vulkan は、該当 Frame が保持する変更されない Descriptor を同一 Binding の Dispatch で共有する。Render Pass へ入る前に記録し、GPU の完了まで旧 Descriptor / Resource を保持する。
- Dense / Sparse SDF は同一 Encoder と同一 Shader 指定の Pipeline を共有する。Dense の出力は Prepare ごとに作り、既存 Texture の参照先を上書きしない。Sparse の出力 Buffer は同一更新内で Readback 完了後に再利用し、未変更の入力では候補タイル再構築を省く。局所編集では既存の dirty tile 判定を維持する。

#### API の利用例

```cpp
auto timestamp = Raven::CreateRef<Raven::Ref<Raven::RHIComputeTimestamp>>();
auto samples = Raven::CreateRef<Raven::Ref<Raven::RHIComputeReadback>>();
const auto record = [generator, timestamp, samples](Raven::RHIComputeCommandEncoder& commands)
{
    return commands.MeasureGpuWork([&](auto& measured)
    {
        *samples = generator->Record(measured);
        return *samples != nullptr;
    }, *timestamp);
};
```

同一 Encoder で `generator->Prepare` を済ませてから、OpenGL は同じ Context で `record(*encoder)` を呼ぶ。DX12 / Vulkan は `encoder->QueueFrame(record)` で予約し、Frame を提出して完了を観測する。両 Ticket の `Poll` が Ready になってから値を使う。CPU への距離取得が不要な場合は `DenseSDFGenerator::RecordTexture` を使う。

#### 短時間の検証とトラブルシュート

- Phase 04〜06 のコマンドで Compute・Dense SDF・Sparse SDF のバイナリ Shader を生成し、Debug x64 の `RavenUIGPUTest=true` をビルドする。作業ディレクトリを `Root/Root` にして、`RavenUIGPUTests.exe --opengl-compute`、`--dx12-compute`、`--vulkan-compute`、`--dense-sdf=<backend>`、`--sparse-sdf=<backend>` を実行する。SDF の Backend 名は `opengl`、`dx12`、`vulkan`。GPU Validation の環境変数は Phase 04 の手順と同じ。
- `Root.exe --gpu-sdf-fluid-demo` で OpenGL の粒子と SDF 断面を表示する。`--test-gpu-sdf-fluid` と `--test-thermal-grid` は短時間の回帰に使う。OpenGL Context 作成が失敗する場合は、GPU が利用できるデスクトップセッションで実行する。
- Shader パスのエラーでは実行時の作業ディレクトリと `Root/Tests/x64/ComputeShaders` の生成物を確認する。Timestamp が Pending のときは Frame の完了通知または GL Context の実行状況を確認する。空の Ticket はデバイスの対応状況を確認する。Sparse の公開世代は全 Batch の完了後に進む。



#### 短時間の測定結果

Windows / RTX 3080 の Debug x64 で実行した。各時間は一回の検証実行の観測値。特にホスト側時間は Frame 完了通知、検証レイヤー、ポーリング間隔の影響を受ける。

| Backend | Compute GPU 時間 (µs) | Compute Readback 観測 (µs) | 閉じた箱 Dense / Sparse GPU 時間 (µs) | 閉じた箱 Dense / Sparse Readback 観測 (µs) |
| --- | ---: | ---: | ---: | ---: |
| OpenGL | 13.3 | 161 | 338 / 119 | 7,859 / 45,643 |
| DirectX 12 | 68.0 | 89,614 | 238 / 112 | 4,224 / 13,142 |
| Vulkan | 22.3 | 27,870 | 444 / 110 | 10,833 / 42,270 |

- DX12 / Vulkan の Compute は 6 Dispatch、Descriptor 確保 4 回・再利用 2 回。従来の毎 Dispatch 確保では同じ記録列で 6 回確保する。計測した論理確保量は Buffer 76 B、Texture 64 B、Readback 152 B。OpenGL は 2 Dispatch、Buffer 76 B、Texture 32 B、Readback 76 B。
- Dense SDF の 6 ケースで Pipeline 確保は 6 回から 1 回。3 Backend とも Buffer 360,928 B、Texture 359,248 B を確保し、数値比較と断面生成に合格した。
- Sparse SDF の局所編集は符号あり・なしとも 225 タイル中 125 タイルを更新した。同じ 8 Batch の条件で Storage Buffer 確保は 16 回から 9 回、編集時の Pipeline 確保は 1 回から 0 回。変更前の確保数は同じ操作列に対する実装上の回数。未変更入力の再公開は 0 Dispatch。閉じた箱の初回生成は累積 Storage Buffer 49,472 B、ピーク論理ペイロード 26,960 B、CPU の公開ペイロード 53,600 B。
- Debug / Release x64、Debug GPU 統合構成、3 Backend の Compute / Dense / Sparse、既存 Scene / Physics / Thermal / UI、OpenGL 流体・熱連携が通過した。DX12 GPU-Based Validation と Vulkan 同期検証で対象の Validation error は 0 件。Vulkan SDK のレイヤー設定 `khronos_validation.validate_sync = true` を使用し、Loader ログでレイヤー挿入を確認した。


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

- 各 Phase の完了時に、その Phase の成果物と検証結果をまとめた小さな PR を作成する。少なくとも Field 契約、RHI 共通 API、Backend、SDF、Physics、電磁気は個別の PR で扱う。
- 各 PR で **目的 / 対象ファイル / テスト / Backend差異 / 未解決リスク** を記録。
- CPU Reference と GPU 実装を比較し、浮動小数点許容誤差を明示。
- Compute→Graphics、Compute→Compute、Compute→Readback の同期を個別にテスト。
- Solver は Field を利用するが、Field の評価関数内で Solver の時間発展を起動しない。
- 実装していない API・テストを「実装済み」と記録しない。
- 実装と実機検証の結果は、該当する Phase の記録に追記する。

## 7. 次回着手時のチェックポイント

1. 最新 `master` と `RHI_MAINLINE_ROADMAP.md` の未完了項目を照合。
2. Debug / Release ビルドと 3 Backend Smoke Test の実測状況を確認。
3. `PhysicsField.h` と `TemperatureField.h` の互換性要件を固定。
4. `RHIDevice`、`RHISceneCommandList`、Explicit Runtime の Compute 統合点を確定。
5. この文書の Phase 00 監査記録を起点とし、Phase 01 の詳細設計は別マイルストーンとして開始する。
