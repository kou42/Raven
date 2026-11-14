# Diagnostics Logger v1（M1）

## 責務と移行範囲

既存の`DiagnosticAPI / EventBuffer / DiagnosticCollector`を拡張し、発行側と出力先を分離する。
データフローは`RHI / Physics → Emit → Thread-local SPSC Buffer → Collector Worker → Sink`。
Coreの基礎型・AllocatorからDiagnosticsを呼ばず、循環依存やAllocator内からの再帰ログを避ける。

調査した既存経路:

| 経路 | 現状と移行方針 |
| --- | --- |
| `Renderer.cpp`のLegacy RHI初期化失敗 | 既存共通APIを継続。ID=`0x1001` |
| `PhysicsWorld.cpp`の重力更新 | 既存共通APIを継続。ID=`0x2001` |
| `VulkanInstance.cpp`のValidation callback | M3で所有する型付きValidationへ変換。Callbackは引き続きVK_FALSEを返す |
| `DX12Device.cpp`のInfoQueue / Live Objects | M3でInfoQueueを共通化。Live Objectsのnative reportと既存stderrは維持 |
| `MemoryTracker.cpp`等の`assert` | Safety Checkを維持。Assert停止動作の変更は含めない |
| `CPUProfiler`とStatisticsPanel、Physics Counter | 現行frame snapshotを維持し、M2で共通Frame IDと試験箇所のAdapterを追加。全面移行はM3以降 |
| `Debug/BrowserDebug*`のSVG / HTTP表示 | 別用途の既存診断機能として維持 |

## API契約

```cpp
#include "Raven/Diagnostics/BinaryTrace.h"
#include <iostream>

using namespace Raven::Diagnostics;
std::vector<std::unique_ptr<IDiagnosticSink>> sinks;
sinks.push_back(std::make_unique<ConsoleSink>(std::cout));
sinks.push_back(std::make_unique<FileSink>("session.log"));
sinks.push_back(std::make_unique<BinaryTraceSink>("session.rvndiag"));
DiagnosticSession session(std::move(sinks)); // Applicationより先に生成する
RAVEN_LOG_INFO(DiagnosticCategory::Physics, EventIds::PhysicsGravityUpdated,
    std::string("Physics gravity updated"));
```

- APIは`category, stable event ID, owned payload`の3引数。書式文字列と可変引数のAPIはM1にはない。
- Log Payloadは`int64 / double / bool / std::string`の単一値。文字列は1024 byteまで保持し、正しいUTF-8入力を途中の継続byteで切断しない。M2の固定長計測Payloadは後述。数値・文字列の複合引数schemaはM5で扱う。
- IDは0を未登録とし、保存イベントには非0の安定IDを指定する。既存IDを再利用しない。
- Timestampは`steady_clock`のnanosecondsで、同一process内の比較用。UTC時刻ではない。
- Thread IDはprocess内で単調採番し、終了したThreadのIDを再使用しない。OS thread IDではない。
- Sequenceはprocess全体の通算番号。異なるThread間ではQueueの収集順と一致しない。
- Category / Severity FilterはSinkごとに構築時に固定する。対象外イベントもBufferへは発行される。
- `SetDiagnosticsEnabled(false)`はログマクロのPayload式を評価しない。`RAVEN_ENABLE_DIAGNOSTICS=0`ではマクロ自体を除去する。直接`Emit`に渡す引数は通常のC++引数として評価される。

## 所有権・並列性・終了

- 各Producerは自分のBufferだけをPushし、CollectorだけがPopする。slot公開はrelease/acquire、Consumerが読み終えたslotだけを再利用する。
- Buffer容量はThreadごとに256。満杯なら新規イベントを破棄し、既存イベントを保持する。Dropは`GetAsyncDroppedCount()`でprocess通算値を確認できる。
- TLSとCollectorがBufferを共有所有する。Thread終了時にCloseし、最後のイベントをDrainしてからCollectorが登録を解除する。
- Sinkは`unique_ptr`でCollectorへ移譲する。ConsoleSinkのostreamだけは借用で、Stop完了まで生存させる。
- `FlushDiagnostics()`は呼出時点までにPush済みのイベントを排出し、SinkのFlush完了まで待つ。API内部の制御mutexが新しいEmitとFlush/Stopを直列化する。
- `StopAsyncLogging()`前に全Producerを停止・joinする。`DiagnosticSession`をApplicationより前に生成すると、ApplicationのmemberであるJobSystemのjoin後にCollectorを停止できる。
- Collector単体を使う場合もStart/Stop等のlifecycle呼出しは外部で直列化する。SinkのWrite/FlushからDiagnostics APIへ再入しない。
- Stop後は同期Consoleへ戻る。Start失敗時も同期経路を残す。Sessionのネストは内側を非所有にし、外側を誤停止しない。
- 所有Sessionの生存中に手動Stop/Startを混在させない。出力先の変更はSession終了後に新しいSessionを開始する。
- Write/Flush失敗・例外のSinkは停止し、`GetDiagnosticSinkFailureCount()`に1件を加算する。他のSinkは継続する。Collected件数はQueueから消費した件数で、全Sinkへの配送保証ではない。
- `WriteEmergencyDiagnostic()`はQueueとCollectorを経由せずstderrへbest-effort出力する。Sink故障時もこの経路を使用し、再帰を避ける。
- Emergency出力はsignal-safeではなく、破損processの回復・Crash Capture・異常終了時の全件保存は保証しない。これはM6の対象。FlushはC++ streamのFlushで、電源断への耐久性は保証しない。

## Binary Trace v1.0

全整数はlittle endian、doubleはIEEE754 binary64。C++のpadding、enum ABI、variant indexは保存しない。

| File Header（16 byte） | 表現 |
| --- | --- |
| Magic | `RVNDIAG`と終端0の8 byte |
| Major / Minor | u16=1 / u16=0 |
| Reserved | u32=0 |

各Recordは`recordBytes:u32`に続き、次の40 byteのHeaderとPayloadを持つ。

| Event Header | 表現 |
| --- | --- |
| Event ID / Timestamp / Thread ID / Sequence | 各u64 |
| Category / Severity / Payload Type / Reserved | 各u8、Reserved=0 |
| Payload Bytes | u32 |

Payload Typeは1=`int64`（8 byte、2の補数）、2=`double`（8 byte）、3=`bool`（1 byteの0/1）、4=`string`（0〜1024 byte、終端なし）。文字列は長さで保存し、埋込NULも保持する。

ReaderはHeader/Record長、enum、型ごとの長さ、Reserved、Versionを検証する。未知Versionは`UnsupportedVersion`、途中切断・上限超過・未知型は`InvalidData`。Record境界でのEOFだけを`EndOfStream`とする。失敗時は出力eventを変更しない。入力の長さに比例したHeap確保を行わず、固定上限の作業領域で読み取る。将来の形式を暗黙にv1として解釈しない。

## 検証

Visual StudioのMSVC C++ toolsetが必要。専用RunnerはGPU / Window / Vulkan SDKに依存しない。

```powershell
./scripts/test-windows-diagnostics.ps1 -Configuration Debug
./scripts/test-windows-diagnostics.ps1 -Configuration Release
./scripts/test-windows-diagnostics.ps1 -Configuration Disabled
```

Debug / Releaseとも例外ベースのチェックを実行し、`NDEBUG`でも発行・検証を省略しない。M1の2 suiteにM2/M3を加え、現在は4 suiteを実行する。
Console書式、所有Payload、UTF-8境界、SPSC並行FIFO、Overflow、Collector再起動禁止、API再起動、4 Thread負荷とDrop整合性、終了Buffer回収、複数Sink、Filter、通常終了Flush、I/O失敗・例外、Binary全型のround-trip・固定byte・全位置の途中切断・不正入力を検証する。

Disabled構成はPayloadの副作用が消えることと、APIの無効状態を検証する。各構成で単純ループと無効化ログ付きループの概算ns/callも記録する。microbenchmark値には実行環境や最適化の影響があり、性能保証には使わない。
ログは`artifacts/diagnostics/<Configuration>/`。Sink故障注入時の4行のstderrと、M3で追加したNative callbackの`(expected)`付きstderrは期待されるfallback出力。

Engine全体は既存の`./scripts/build-windows.ps1 -Configuration Debug -SkipBootstrap`（Releaseも同様）でビルドし、`Root/x64/<Configuration>/Root.exe --test-diagnostics`で同じCPUテストを実行できる。

未検証のRuntime確認は、OpenGL / DX12 / Vulkan通常起動からの正常終了、Scene破棄中のログ保持、実機Validationエラー時の既存stderr動作。M1ではBackend Validationの全面移行やGPU障害注入は行わない。

## 残る制約

M1のEmitは登録・Start/Stopの安全性を優先してprocess共通mutexを取得し、string PayloadはHeap確保を伴い得る。lock-free / allocation-freeなHot Pathは保証しない。M2では計測用の固定長Payloadを追加したが、初回登録と共通mutexは維持する。
Buffer数は同時に生存するProducer数に比例する。SinkのI/Oが無期限に停止するとFlush/Stopも待機するため、任意のTransportへのtimeout・切断復旧はM6で設計する。

## M2: Counter / TraceとCPUProfiler接続

`DiagnosticMeasurement.h`の`RAVEN_COUNTER(name, value)`と`RAVEN_TRACE_SCOPE(name)`を使用する。
計測は既定で無効。Sessionを開始してから`SetMeasurementsEnabled(true)`で有効化する。
通常Applicationは`--diagnostics-measurements`でConsole計測、`--diagnostics-trace=path.rvndiag`でBinary Trace記録を有効化できる。保存時のConsoleはLogだけを表示し、高頻度計測の重複出力によるCollector遅延を避ける。保存先の親ディレクトリは事前に用意する。
専用の`--scene-*`／`--clear-*` Demo入口への設定引継ぎは含めない。

```cpp
#include "Raven/Diagnostics/DiagnosticMeasurement.h"
#include "Raven/Diagnostics/MeasurementTimeline.h"

using namespace Raven::Diagnostics;
// Sessionと出力先を用意した後に有効化する。
SetMeasurementsEnabled(true);
BeginDiagnosticFrame();
{
    RAVEN_TRACE_SCOPE("PhysicsStep");
    RAVEN_COUNTER("Physics.Candidates", 42.0);
}
FlushDiagnostics();
// Readerから復元した所有イベント列をeventsに渡す。
// auto timeline = ReconstructMeasurementTimeline(events, captureDroppedEvents);
```

### 計測PayloadとScopeの契約

- `DiagnosticMetric`をvariantの新しい型として追加した。Kind（Counter/TraceBegin/TraceEnd）、Frame ID、Scope ID、Parent Scope ID、double値、最大96 byteの名前を値で保持する。
- 名前は正しいUTF-8を前提に境界で切り詰める。長い名前の先頭が同一なら表示上は区別できないため、診断名は96 byte以内で一意にする。
- Counterは有限のdoubleのみ。整数の完全な表現は2^53までという既存CPUProfilerと同じ制約がある。
- Begin/Endはprocess全体で単調採番するScope IDで対応付ける。親IDはThread-localで管理し、Threadを跨いでScopeを移動しない。Scope型はcopy/move不可。
- Endは開始時のFrame ID・親ID・名前を保持する。ScopeがFrame境界を跨いでも開始Frameに属し、実時間はBegin/End双方のtimestampで表す。Frame ID=0は最初のFrame開始前を表す。
- `Renderer::BeginFrame`で共通Frame IDを採番し、`CPUProfiler::BeginFrame(frameId)`へ渡す。Profilerの無効化・再有効化後も共通番号を使う。引数省略（0）は既存のProfiler内採番を維持する。
- 開始後に計測やLogを無効化しても、受理済みBeginに対するEndは同一Sessionへ送る。BeginがDropされたScopeはEndを送らない。EndだけがDropされた場合は未完了区間になる。
- Session停止を跨いだEndは新Sessionへ送らない。新Sessionでは親IDをリセットし、旧SessionのScopeが残っていても新Traceの親にしない。
- 計測APIはCollector稼働中だけ発行し、停止中の同期Consoleへfallbackしない。Payload自体はHeap確保しないが、初回Thread登録はBufferを確保し、M1の共通制御mutexも維持する。
- `RAVEN_ENABLE_DIAGNOSTIC_MEASUREMENTS=0`でCounter/Traceだけをビルド時無効化できる。Logは有効のまま。`RAVEN_ENABLE_DIAGNOSTICS=0`は両方を無効化する。マクロの名前式・値式は無効時に評価しない。
- `ProfiledTraceScope`は既存CPUProfileScopeとTraceを併記するAdapterで、CoreからDiagnosticsを参照しない。計測無効時も従来Profilerの挙動は残す。Runtime無効時はTraceの固定Payload初期化を省略し、ビルド時無効時はAdapterのTrace memberも除く。

試験導入は`PhysicsWorld::Step`のCPU区間と末尾のManifold数、`RHISceneMeshRenderer::FinishActiveFrame`内のSubmit区間・成功値に限定した。Submit区間は`EndFrame()`だけを囲み、PresentやGPU完了待ち全体の時間として解釈しない。既存StatisticsPanelは従来のProfiler snapshotを使い続ける。入れ子Scopeの時間は重複するため、全Scopeを単純加算してframe総時間にはしない。

### Binary Trace v1.1

Writerのminorを1へ更新した。LogのHeader/Record/Payload（tag 1〜4）はv1.0から変更せず、新Readerはminor 0/1を受け付ける。M1 Readerはminor 1を拒否するため、未知型を誤解釈しない。

計測Payloadはtag 5。`kind:u8, reserved:u8=0, nameBytes:u16, frame:u64, scope:u64, parent:u64, value:binary64, name[nameBytes]`の36 byte Headerと名前からなる。名前は最大96 byte。未知Kind、不正長、空名、非有限値、Counterの非0 Scope ID、Traceの0 Scope ID・自己参照親ID・非0値を拒否する。v1.0 Headerにtag 5が現れた場合も拒否する。

### Timeline再構築と欠落

`ReconstructMeasurementTimeline()`は所有イベント列からCounter sampleとThread別Trace区間を作る。入力数の上限を確保前に検査し、Scopeの対応付けはThread IDとScope IDで行う。到着順が逆でも復元し、結果はThread/開始時刻順に並べる。
片側欠落はMissingBegins/MissingEnds、親の欠落はMissingParents、重複端点や逆転時刻・不一致metadataはInvalidEventsで報告する。未完了区間に有効な所要時間があると扱わない。Frame別集計は各sample/区間のFrameIdで行える。

Begin/Endが両方消えたScopeはイベントから推測できない。取得中の`GetAsyncDroppedCount()`の差分を引数へ渡して完全性を評価する。Drop差分はv1.1ファイル自体には自動保存しないため、保存イベントだけで「一切欠落なし」とは判定しない。Session境界を跨いで複数processのファイルを混在させる用途も含めない。

### M2検証

専用RunnerはDebug/ReleaseでM1を含む3 suiteを実行する。
追加検証は入れ子、4 Thread、Frame跨ぎ、無効化中のEnd、Session跨ぎ、既存ProfilerのFrame/Result/Counter維持、名前上限、不正計測record、v1.0互換、保存再読込、順序変更、片側Drop、入力上限。
`-Configuration MeasurementsDisabled`でLogを残した計測無効化も確認できる。CIのDiagnostics jobで4構成を検証する。

有効時の測定はNull Sinkへ64件ずつ発行し、batch間Flushと初回登録を計測区間から除外する。Counter 1回とTrace Scope 1区間（2イベント）のns/callを別々に出し、Drop=0を検証する。Runtime無効時は名前・値の副作用が0であることも確認する。これはCPU発行コストの参考値で、File I/Oや全Subsystem計測時の性能保証ではない。

M2で残した未検証事項はGPU実機の保存・通常終了、実GPU Submit精度、長時間の大規模Trace。M2にはM3の全面接続とM4のEditor Timeline表示を含めなかった。

2026-11-14のローカル検証は4構成の専用Runnerと、Engine Debug/ReleaseのDiagnostics、DebugのExplicit Frame/Physics Collision、CPU UI Debug回帰が成功した。全体ビルドは`RavenGenerateLinkDebugInformation=false` / `LinkIncremental=false`を指定した。UI再ビルドは不足するGLFW 3.4 native headerをキャッシュ`artifacts/sg3-deps/GLFW/glfw3native.h`から検証用propsで追加参照した。プロジェクトの依存設定は変更していない。

## M3: Subsystem Integration

### 発行設定

`SetDiagnosticCategoryEnabled(category, enabled)`で共通イベントの発行を切り替える。SoftBodyは新しいcategory値7を使い、Physicsとは独立する。既存のcategory値0〜6は変更していない。
通常Applicationの`--diagnostics-disable=rhi,physics,softbody,renderer`で指定したSubsystemの共通発行を無効化できる。複数指定・複数引数は加算し、未知名・空名は起動前に拒否する。計測の有効化は従来どおり`--diagnostics-measurements` / `--diagnostics-trace=path`で別に行う。

設定は診断発行だけに作用し、Physicsの計算や既存CPUProfiler・Statistics表示・Native Debug Layerを変更しない。切替はatomicで、既に開始した発行と受理済みScopeのEndは切替後も残り得る。RHIの共通収集を無効化しても、Native Validationのstderr出力は安全用fallbackとして残す。
名前の接頭辞からcategoryは推測しない。M2の2引数Counterマクロと1引数TraceマクロはRuntime categoryなので、Subsystemへ接続する場合は`RecordCounter(name, value, category)` / `TraceScope(name, category)`等で明示する。

### RHI Validation

`DiagnosticValidation`はBackend、元の符号付きMessage ID、Native Severity/Type、観測時Frame ID、名前、本文、切詰めフラグを所有する。VulkanのERROR/WARNINGは共通Error/Warning、INFO/VERBOSEはInfoへ対応する。DX12はCORRUPTION/ERRORをError、WARNINGをWarning、INFO/MESSAGEをInfoへ対応し、元の数値も保持する。
Vulkan callbackの戻り値、既存のVerbose条件、DX12 InfoQueueの取得後Clearは維持する。Frame IDはCPUで観測した時点の番号で、GPU上で原因が発生したFrameの特定は保証しない。

本文は最大1024 byte、名前は128 byteを正しいUTF-8の境界で保持し、切り詰めたことを記録する。Native pointerは保持せず、callbackやGetMessageの一時storageが破棄された後も保存・表示できる。
Callbackの発行は制御mutex・登録mutexのtry_lockを使う。停止中、競合、Queue満杯、再入、確保失敗ではstderrへbest-effortで退避し、Native ABIへC++例外を返さない。`GetValidationFallbackCount()`で退避回数を観測できる。退避内容と退避回数はBinaryへ自動保存しない。
この専用Validation入口はWorkerのSink処理から発生するNative callbackにも対応するが、Sinkから一般のLog/Flush/Start/Stopへ再入する禁止事項は維持する。stderrはsignal-safeではなく、process破損時やOS I/O停止時の保存保証はない。
Live Objectsのnative report、Device初期化等の他の既存メッセージを一括置換するものではない。

### Counterの意味と境界

| 発行元 | Counter | 意味 |
| --- | --- | --- |
| Physics DetectCollisions | BroadPhasePairs | 有限形状のBroadPhase候補数 |
| 同上 | FiniteNarrowPhase.ValidPairs | 有効な両Colliderを確認した候補数。形状判定関数の呼出数・接触数ではない |
| 同上 | PlanePairTests | 無限Planeとの候補判定仕事量 |
| Physics Step末尾 | Manifolds / ContactPoints / WarmStartedConstraints / VelocityIterations | 既存Solver統計値。M2のManifoldsを重複発行しない |
| SoftBody Step末尾 | Particles / DistanceConstraints / DihedralConstraints / SolverIterations | 現在のTopology規模と実際の反復設定 |
| StepWithSelfCollisions末尾 | ParticleTriangle.Candidates / NarrowPhase | 全iterationの仕事量合計 |
| 同上 | ParticleTriangle.DetailedCollected | 詳細収集あり=1、未収集=0 |
| 同上（詳細収集時のみ） | ParticleTriangle.Constraints / Corrections | 既存のConstraintCount / PositionCorrectionCount |
| Renderer Frame終了 | DrawCalls / Indices / Triangles | 既存RendererStatisticsの発行済み描画量 |
| 同上 | RHI.LiveWrappers | RHIResourceを継承する生存C++ラッパー数 |

接頭辞はそれぞれ`Physics.Collision.*` / `Physics.Solver.*` / `SoftBody.*` / `Renderer.*`。内側ループではログを発行せず、計算済みの値をStep末尾等で渡す。候補・制約仕事量はcatch-upの複数Stepを合算できるが、Topology規模・反復設定・生存数はsnapshot値として扱い、単純合算して実体数と解釈しない。

Constraint/Correctionの詳細統計は既存の`DetailedParticleTriangleProfilingEnabled`に従う。Diagnosticsがこの設定を勝手に有効化しない。未収集値を0件として発行せず、DetailedCollected=0で区別する。通常Step / Volume Stepから前回のParticle-Triangle統計は送らない。
RendererはMain ThreadのFrame guardで正常終了・break・return時にも発行し、同じSession/Frameの重複発行を防ぐ。途中失敗Frameにも既に発行した描画量は含む。GPU成功・完了した描画数ではない。

Resource数はRHIResourceのbase constructor/destructorでatomicに増減する。Buffer/Texture/Pipeline/Offscreen Target等のC++ラッパーを対象とし、所有参照を増やさない。外部参照が残ればnative handleを無効化した後も数える。Device/Queueやnative allocationの数・使用byte数・Fence完了・リークの有無は表さない。Shared ownerの別名は二重計数しない。

### Binary Trace v1.2

Writerのminorを2へ更新した。新Readerはminor 0/1/2を読み、旧Readerは未知minorを拒否する。Log / Metricの既存配置は変更しない。新しいSoftBody categoryとValidation tag 6はminor 2だけで受け付ける。

Validation Payloadは`backend:u8, flags:u8, nameBytes:u16, messageBytes:u16, reserved:u16=0, nativeId:i64, nativeSeverity:u32, nativeType:u32, frame:u64, name, message`。Headerは32 byte、flagsのbit 0が名前切詰め、bit 1が本文切詰めで、他bitは0。長さとBackend、Reserved、Flagsを検証し、最大Payloadは1184 byte、Event Headerを含むRecord上限は1224 byte。
上限拡張後も従来Logの文字列は1024 byteまでとする。旧minorの上限は従来どおりで、v1.0/1.1 Headerに新型や新categoryが混入した入力も拒否する。

### M3の検証と制約

専用RunnerのDebug/ReleaseはM1/M2を含む4 suite。追加検証はNative Severity変換・ID/本文の所有・切詰めフラグ、保存再読込、固定byte、全位置での途中切断、不正長・Backend・Flags・Reserved、旧minor互換、Category独立性、受理済みEnd、停止後fallback、Worker Flush中のNative callback、共有参照・複数ThreadのResource寿命。
`--test-explicit-frame`では衝突する2 SphereとCloth自己衝突を使い、共通Counterと実統計値の一致・診断有効/無効での結果不変性・詳細未収集の区別を確認する。Renderer統計のTopology別計数・Resource値・二重発行防止もGPUなしで検証する。
`--test-softbody-integrated`で既存のSoftBody統合回帰を単独実行できる。CIにもDiagnostics・Explicit Frame・Physics Collision・SoftBody統合の実行入口を追加した。

GPU実機のValidation故障注入・Frame終了・native Resource解放完了の確認は未検証。共通mutexと初回Buffer確保、stderrだけへ退避したValidationのファイル欠落、GPU側の原因Frame不明は残る制約。M4のEditor Viewerは対象外。

2026-11-14のM3検証結果: 専用RunnerのDebug/Release各4 suiteと無効化2構成、Engine Debug/Release x64ビルドと両実行ファイルのDiagnostics各4 suite、Debugの実Subsystem比較・Physics Collision・SoftBody統合、CPU UI Debug回帰が成功した。Subsystem比較では同一入力の有効/無効で結果が一致し、SoftBody詳細未収集を0件扱いしないことも確認した。CLI未知名は終了コード1を確認。
全体ビルドは従来のPDB無効化とGLFW 3.4 native headerの検証用追加参照を使用した。プロジェクトの依存設定は変更していない。全体Debugの既存リンクWarningは2件、Releaseは0件。UIの初回再ビルドには既存C4244が32件とリンクWarning 2件、最終増分ではリンクWarning 2件。専用/W4はalignas由来のC4324がDebug/Release各6件、無効化構成各3件で、今回の診断コードに新規コンパイラWarningはなかった。
