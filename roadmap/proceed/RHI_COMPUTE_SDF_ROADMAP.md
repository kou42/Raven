# Raven RHI Compute / GPU SDF / Physics Field 統合ロードマップ

更新日: 2026-11-19

対象: `kou42/Raven` / `master`

状態: **Phase 00 の基盤監査・起動ベースライン記録済み。Phase 01 の Field / Storage 設計確定。Compute 実装未着手**

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

- 各 Phase の完了時に、その Phase の成果物と検証結果をまとめた小さな PR を作成する。少なくとも Field 契約、RHI 共通 API、Backend、SDF、Physics、電磁気は個別の PR で扱う。
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
5. この文書の Phase 00 監査記録を起点とし、Phase 01 の詳細設計は別マイルストーンとして開始する。
