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

## M4: Editor Diagnostics（2026-11-15）

通常SessionはConsole（Log/Validationのみ）、Editor Store、任意のBinary Traceへ同じイベントを配信する。計測のConsole重複出力は、UIのCaptureを有効にしてCollectorを詰まらせないため抑制する。計測の有効化は既存CLIまたはPanelのCapture measurementsで行う。CategoryのCLI無効化は引き続き尊重する。Store SinkとPanelは共有所有権を持つが、WorkerはWidget/Scene/Panelを参照しない。Panel破棄後もStoreとSessionは生存でき、ApplicationとProducer破棄後にSessionを停止する。

Storeは既定8192 slot、8 MiBの保持予算。固定slot容量と可変長文字列capacityを推定byteに含め、件数/byteのいずれかを超える前にFIFO破棄する。allocator overhead、Snapshot、Model、短命なコピーはこの数値に含まない。設定上限は65536 slot / 64 MiB。本文は既存の1024 byte、Validationは既存の名前128/本文1024 byteまでとし、不正入力はコピー前に拒否する。Snapshotも同じ件数上限内の所有コピーで、Panelは最新1つだけを保持する。Store mutex中にDiagnostics APIを呼ばない。Snapshotはtry_lockで競合時に次回へ延期し、取得後のコピー中はCollectorを短時間待たせる。Lock-freeではない。

Panelは表示中に最大5 Hzで更新し、Freezeは表示のみ止める。仮想化UITableは可視セルだけを書式化する（Search時は保持行を検索）。選択は行番号でなくStore通算IDで保持し、Filterで隠れても維持し、Eviction時は解除理由を表示する。Clear APIも通算IDを再利用しない。Inspector本文は独立Scrollで改行の多いNative本文を読める。Captureは既存measurement有効フラグだけを変更し、高コストのSoftBody詳細収集を自動的に有効にしない。

検索は大文字小文字を区別する本文検索で、入力を256 byteに制限する。Severityは下限。Search/SeverityはLog Tableにだけ適用し、Category/Frame/ThreadはLogとTimelineに共通適用する。Frameは空欄=すべて、`?`=Frame非関連の汎用Log、`0`=起動時の既知Frame。未知Frameを現在Frameに割り当てない。Frame/Threadの不正入力は直前の有効Filterを維持し、注意文を表示する。

Counter Graphは同名の生サンプルをTimestamp順に最大512個表示し、Frame Filterに一致する点を黄色にする。Frame Filter外の履歴は残す。最小/最大値を表示し、Thread混在の加算・平均は推測しない。CPU Timelineはns軸とThreadごとのLaneを持ち、最大2048区間/32 Threadを描画する。Rootと子を分け、より深い階層は同じ子段へ重ねる。時刻範囲とLaneのThread IDを表示する。緑は完全なBegin/End、橙は不完全な点で、未記録時間を補間しない。測定行を表示・選択するとScope/Parent IDと完全な区間のns時間を読める。Zoom/階層展開/区間クリックは未提供。

完全性は別々に表示する: StoreのEvicted/Rejected（Clear以降）、process累積Queue Drop/Native stderr fallback、選択範囲のMissing Begin/End/Parent/Invalid。不明な区間の時間や、未収集の詳細Counterを0と扱わない。両端が欠落したScope、stderrだけへ退避したValidationはViewerへ復元できない。FrameはCPU観測値でGPUの原因Frameを保証しない。

新しい診断5番目のsuiteは、Collector→Store配信、件数/byte制限、所有コピー、破棄後の選択、Clear後ID、不正入力、未知Frame、RHI Error検索と同Frame Physics Trace、Counter表示上限、並行Snapshot、10万イベント負荷を確認する。専用RunnerへUI非依存のDiagnosticsViewModel.cppを追加した。CPU UI回帰は実入力経路のSearchと選択、Detach後のStore寿命、8192イベント/40 Threadでの2種類のChart描画上限を検証する。Dockの7 Panel復元と既存の保存/切り離し回帰も実行する。

検証結果: 専用Debug/Release各5 suite、Disabled/MeasurementsDisabled各1 suite、Engine Debug/Release x64と両Diagnostics 5 suite、DebugのExplicit Frame/Physics Collision/SoftBody統合、CPU UI Debugが終了コード0。10万件投入からSnapshot/Modelまでの単発計測は専用Debug約79.8 ms / Release約7.9 ms、既定Store保持推定1638400 byte（1.56 MiB）。Counter主体の入力であり、全入力に対する上限時間ではない。8192イベント・40 Thread・3072 Trace区間のSnapshot/Model/Chart CPU更新はDebug約59.6 ms。5 HzでもDebugの飽和時にはstutterの可能性があり、必要時はFrame/Thread FilterとFreezeを使う。性能保証/合否時間閾値は設けていない。

全体ビルドは従来のPDB無効化とGLFW 3.4 native headerの検証用追加参照を使用した。既存リンクWarningはDebug/UI各2、Release 0。UIテスト再コンパイルの既存C4244は32件。専用/W4の既存C4324はDebug/Release各6、無効化各3。新規Warningはない。実GPU画面はcomputer-useでRelease実行ファイルの起動を試したがtargetable windowがなく、fresh inventoryにもRavenは存在しなかったため、描画・Dock操作の実画面確認は未検証。Native Validation故障注入はM3同様未検証。M5以降は対象外。

## M5: Structured Message Database（2026-11-15）

`Messages.json`を日英文言・安定ID・引数Schemaの原本とし、`Messages.lock.json`で過去のID/Key/Schema版/hashを予約する。生成器は`Generated/MessageMetadata.generated.h/.cpp`へ型付きFactoryと読み取り専用Metadataを生成する。生成ファイルはレビュー・配布用にコミットする。通常ビルドと専用Runnerは`-Check`で原本・Lock・生成物の一致を検査し、古い生成物のままビルドしない。PowerShell 5.1/7対応で、新しいツール依存は追加しない。生成Metadataだけを.gitattributesでLF固定し、Windowsのautocrlf付きcheckoutでも-Checkのbyte一致を維持する。

```powershell
./scripts/generate-diagnostic-messages.ps1          # 翻訳変更後の再生成
./scripts/generate-diagnostic-messages.ps1 -Check   # CI/ビルドの検査
./scripts/generate-diagnostic-messages.ps1 -UpdateLock # 新ID/新Schemaをレビュー後に履歴へ追加
./scripts/test-diagnostic-message-generator.ps1
```

IDは明示指定（Catalog v1では非zero uint32、Event/wireは従来どおりuint64）。重複ID/Key・過去IDの別Keyへの再利用・予約済みNative/Metric IDの種類変更・生成C++名衝突を拒否する。同一Schema版の引数型/順序/名前・Category/Severity変更を許可せず、新しい版を明示してLockへ追記する。翻訳文はSchema hashに含めず、翻訳だけの修正で過去イベントを無効にしない。Schema hashは`dictionary|id|schemaVersion|category|severity|name:type...`（UTF-8）のSHA-256先頭64 bitの数値表現。互換性照合用で、改竄検出や認証の保証ではない。過去Lock行は予約として維持し、現在のDictionaryに過去全版の翻訳文は埋め込まない。

Runtimeは`DiagnosticMessage`にDictionary版・Schema版/hash・所有する引数を保持し、翻訳文はイベントに保存しない。引数型はint64/double/bool/UTF-8 string、最大8個、string単体256 byte、引数値の合計1024 byteまで。doubleは有限値のみ。長すぎる引数を黙って切り詰めず発行/保存を拒否する。Templateは各言語1024 UTF-8 byte、置換は最大8箇所とし、生成器が未定義/不足/壊れたplaceholderを拒否する。名前は`{reason}`などで指定し、引数本文に含まれる`{...}`を再評価しない。出力は通常の数値表記で整形し、数値localeはclassicを使用する。日英の切替で元のID・引数・時刻・Sequenceは変化しない。

```cpp
#include "Raven/Diagnostics/Generated/MessageMetadata.generated.h"
RAVEN_MESSAGE(Raven::Diagnostics::Messages::PhysicsGravityUpdated,
    Raven::Diagnostics::Messages::MakePhysicsGravityUpdated(0.0, -9.8, 0.0));
```

Factoryは宣言をh、実装をcppへ分離する。`RAVEN_MESSAGE`は診断/Category無効時にFactoryや引数を評価しない。通常Emitterは登録済みSchemaと型を検証してから既存SPSC/Collectorへ渡す。戻り値falseは無効・不一致・Publish失敗を示す。Native callbackのnon-blocking APIには接続しない。Physics重力更新とLegacy RHI初期化失敗を既存IDのまま移行し、Core session状態のSchemaも提供する。旧scalar/string payloadやNative Validation/Metricの形式と経路は維持する。

Console/Fileは既定の英語Formatterを使う。EditorのLanguage選択はPanel単位・既定Englishで、表示と本文検索を切り替え、Snapshotや選択の通算IDを書き換えない。対象は型付きMessage本文であり、Editor全CaptionやNative Driver本文の翻訳は含めない。未知ID/Dictionary版/Schema不一致は理由・ID・版/hash・生の引数へfallbackし、現在の翻訳文を誤って適用しない。Reader/StoreはDictionaryへ依存せず、構造が正しい未知Messageも保持する。

StoreはMessageの引数vector容量と文字列capacityも保持byteへ含める。Snapshotは引数まで深く所有し、元データ変更やStore Clear後も参照切れしない。既定件数/byte予算はM4のまま。Formatterは最大8引数と各文字列上限までしかfallback出力せず、翻訳Templateの置換回数にも生成時上限を設ける。

日本語Templateが既存のASCII Atlasでは表示できないため、起動側がDictionaryの使用文字だけを`ApplicationSpecification.RuntimeUIFontAdditionalCodepoints`へ渡す。Coreは汎用の追加文字集合だけを扱い、Dictionaryを参照しない。Windows起動側は存在すればMeiryo TTCを選び、不在時は既存Segoe UIへ戻して警告する。Atlasは既存のDevice初期化後・Panel Attach前に構築し、Ref共有の寿命を維持する。任意の引数/Native本文の全Unicode文字、外部指定Atlasの不足Glyph、OS Font不在の代替Font配布は今回の対象外。

### Binary Trace v1.3

Header minorを3へ更新し、tag 7へ型付きMessageを追加。Record Header/既存tag 1〜6は変更しない。Message payloadは`dictionary:u16, schema:u16, schemaHash:u64, count:u8, reserved:u8=0, reserved:u16=0`（16 byte）に続き、引数ごとに`type:u8, reserved:u8=0, valueBytes:u16, value`を格納する。typeは1=int64、2=double、3=bool、4=UTF-8 string。数値はlittle endian、boolは0/1。値のbyte合計上限には各型のwireサイズを使用する。最大Message payloadは1072 byte以内で、従来Validationの最大1184 byte/Record 1224 byteを拡張しない。

Readerはv1.0〜1.3を受理し、古いHeaderにtag 7が混入した場合、未知引数tag、Reserved、過大Count/長さ、不正bool、非有限double、zero版/hash、末尾の余分なbyteを拒否する。失敗時は呼び出し元eventを変更しない。未知の非zero Dictionary/Schema版/hashは構造だけ検査して生値を返し、Formatter側で互換性を判定する。旧Readerは未知minorを拒否する既存契約のままなので、新Traceを旧Readerが読めるという後方方向の保証はない。

M5の検証は診断6番目のsuiteで日英表示、全引数型、Binary固定byte/往復/全位置の途中切断・不正入力、旧minor、新Dictionary/Schema不一致、引数上限・Store所有/byte上限、言語別検索と選択保持、無効時の式省略、Collectorへの実配信を確認する。生成器の異常系は独立PowerShellテストとCIへ追加した。CPU UI回帰ではLanguage ComboBox→Modelの接続とSnapshot不変性、Meiryoが存在する環境でTemplate全文字の実Font glyph収録を確認する。GPU上の日本語描画とWindow/Dock操作は別途実機確認が必要。

2026-11-15のM5最終検証結果: 専用Debug/Release各6 suite、Disabled/MeasurementsDisabled各1 suite、Engine Debug/Release x64増分ビルドと両Diagnostics各6 suite、DebugのExplicit Frame/Physics Collision/SoftBody統合、CPU UI Debugが終了コード0。生成器16チェックはPowerShell 5.1/7の双方で成功し、通常ビルドのMetadata検証も成功した。UI Language操作で元Payloadが不変であること、MeiryoのTemplate全文字Glyph収録をCPUで確認した。Byte上限、Snapshotの深い所有、未対応Schemaの生値表示、旧Trace互換も確認した。

全体ビルドには既存PDB問題の回避フラグとGLFW 3.4 native headerのローカル検証用参照を使用し、依存設定の恒久変更は行っていない。新規コンパイラWarningは0。既存WarningはEngine Debugのリンク2件、Release 0件、UIのC4244が32件とリンク2件、専用/W4のC4324がDebug/Release各6・無効化各3件。GPU上の日本語Atlas描画・Dock操作、Native故障注入は未検証。Schema履歴には過去文言を保存しないため、古い構造化Schemaは必要に応じて生値表示になる。M6以降は対象外。

## M6-A: Windows Loopback Remote Diagnostics（2026-11-15）

通常Applicationの`--diagnostics-connect=PORT`でNetworkDiagnosticSinkを追加する。既定は無効で、同一PCのWindows IPv4 Loopbackに限定する。既存のWindowsTcpSocketを再利用し、制御Message固有のReceiveControlStreamだけをWindowsControlStream.cppへ移動した。制御Codecを診断用の専用Runnerへリンクしないための分離であり、元の制御Protocolは変更していない。一般LAN接続・認証/TLS・複数Senderの集約・Remote Editor操作は対象外。

```powershell
# 先に受信側（GPU/Applicationを初期化しない別プロセス）を起動する。
./Root/x64/Debug/Root.exe --diagnostics-receive=45000 --diagnostics-output=capture.rvnd --diagnostics-duration-ms=300000 --diagnostics-max-bytes=67108864 --diagnostics-language=ja
# 別プロセスで通常Editor/Runtimeを起動する。計測は既存の明示設定を使用する。
./Root/x64/Debug/Root.exe --diagnostics-connect=45000 --diagnostics-measurements
# GPUなしの固定5イベント試験と保存ファイル検査の入口。
./Root/x64/Debug/Root.exe --diagnostics-send-test=45000
./Root/x64/Debug/Root.exe --diagnostics-inspect=capture.rvnd
./scripts/test-diagnostics-remote.ps1 -Executable ./Root/x64/Debug/Root.exe
```

### 所有権・Queue・終了

Collectorは符号化したイベントを所有Queueへ渡すだけで、接続/送信完了を待たない。Queue mutexは更新・取り出し・Stats snapshotのみに使用し、Socket I/O、Counter取得、再接続はLock外。既定2048件/符号化2 MiB、設定上限65536件/64 MiB。Queue byte値はBinary符号化byteであり、deque node/string capacity、Workerが処理する最大1件、Packet生成用の上限付き作業領域は別途有界となる。満杯時は新しいイベントを破棄し、通算OrdinalとDrop Counterで検出する。Producerの既存SPSC Dropとは別に数える。

Network Sinkだけが専用Workerを持ち、SocketはそのWorker内で生成・操作・破棄する。Sink Write/Flushから通常Diagnostics APIへ再入しない。Workerが参照するGetAsyncDroppedCountはprocess累積atomic snapshotへ変更し、Session制御mutex内でSinkを破棄する際のjoinとの相互待機を防ぐ。Worker例外は独立stderrへ退避し、fatalのNetwork Sinkを他Sinkと切り離す。一時切断はSink自体のfatal扱いにせず、再接続する。通信障害中でも同じCollectorのローカルStore/Binary/Consoleは処理を続ける。

FlushはQueue処理を促すだけで、Peer到達・保存・Network Queue排出を保証しない。通常のSession終了順はProducer終了→Collector排出→Network Worker停止。StopはI/Oの100 ms絶対期限と、Stopを観測してから250 msのdrain猶予を使用し、残Queueを破棄・計数してjoinする。再試行待機は50 ms。OS Scheduling、Allocator、ローカルFile I/Oのリアルタイム上限を保証するものではない。WriteとStopの同時発行は新規イベントをDrop扱いにするが、Stop同士/破棄は呼出側で直列化する。終了時は独立stderrへattempted/enqueued/os_sent/dropped/connections/connect_failures/disconnects/last_error/native_errorを出す。GetStatisticsでも参照でき、Worker稼働中の複数Counterの読取は瞬間的な不一致があり得る。

### Protocol v1と欠落

各Packetはlittle endianのbodyBytes:u32と64 byte Headerに続く。Headerはkind:u8（Hello=1/Event=2/Status=3/Close=4）、version:u8=1、reserved[6]=0、sessionHigh:u64、sessionLow:u64、connection:u64、ordinal:u64、networkDropped:u64、producerDroppedProcess:u64、senderOsSent:u64。Sessionは生成時刻・process ID・process内通算値で識別し、暗号学的な認証値ではない。再接続でも同じSession、Connection番号だけを増やす。Frame/Thread/Scope/Timestamp/Message IDは送信元の値を保持し、受信側の時計やFrameへ変換しない。

Event bodyには既存Binary Headerとちょうど1 Eventを格納し、その他kindはpayloadなし。最大Event Bytes=1244、Remote body=1308、Prefix込み=1312 byte。Parserは最初の4 byteで長さを検証してから上限付き領域へ受信し、分割/結合受信、版・Reserved・型・Binaryの長さ/末尾を検証する。独立Receiverは接続ごとに新Parserを作り、Hello前のEvent、Session混在、Connection/Ordinalの巻き戻りを拒否する。1 captureは最初の1 Sessionに固定し、他Sessionを同じTraceへ混ぜない。

SendAll成功はOSの受付だけを意味する。ACK・永続化確認・再送保証は提供しない。部分送信失敗時は接続を閉じ、そのイベントをDrop計数し、次の接続で再送しない。これにより重複解釈を避ける。受信Ordinalの空白（末尾はCloseで確定）、送信側Network Drop、process累積Producer Drop、受信接続の中断・Protocol拒否、受信件数/保存件数を別々に記録する。StatusはQueueが空の接続中に概ね100 ms間隔で送り、Eventにも同じCounter snapshotを含める。Closeを受信できなかった場合、最後の報告以降の欠落数やSenderの最終状態は不明のままとし、完全なCaptureとは扱わない。

### 保存と受信期限

受信側は指定したBinaryファイルと`.remote.json` sidecarを、最初の正常HelloでCREATE_NEWにより作成する。どちらかが既存なら拒否し、検査とopenの間の競合でも上書きしない。Binaryはv1.3形式を維持し、sidecarへSession ID、受信数、保存byte、Ordinal gaps、Drop値、中断/拒否数、Close/保存失敗状態を保存する。IDはJSON文字列として精度を維持する。sidecarは入力文字列を含まない固定項目の小さな文書で、Binaryの保存byte予算とは別枠。失敗時の部分Captureは調査用に残し、自動削除しない。

Binary保存量は既定64 MiB、CLI上限1 GiBでHeaderを含む。全体受信時間は既定300秒、設定範囲100 ms〜1時間。Acceptを最大4096回試行する上限もある。1接続の各Frameは最大1秒の絶対期限を使い、少量の部分受信で延命しない。保存上限/保存失敗/Closeで終了し、切断や不正接続ではSessionを維持して再接続を待つ。正常Closeかつ保存成功なら終了コード0、それ以外は1。Binary/sidecarのflushやOS File I/O障害時に永続化を保証しない。

### M6-A検証

専用の診断7番目のsuiteはPrefix/Bodyの全分割位置、途中切断、結合、未知版/不正長/Reserved/Binary、不読Peer時のQueue上限とStop、接続拒否からの復旧、Network切断中のローカルStore継続を確認する。別プロセス試験は固定5イベント（Log、型付きMessage、Counter、Trace Begin/End）の転送・日英表示・保存再読込・sidecar、既存ファイル拒否、保存上限、無通信期限、不正通信・Session混在・再接続の5シナリオを確認する。CIでは専用Debug/Releaseの双方を実行する。

Loopbackはこの環境の実行サンドボックス内で接続に失敗し、同じ実行ファイルが制限外で成功することを確認した。実通信の結果はすべて制限外の実行に基づく。GPU、別PC、Crash Capture、長期検索、Node ID連携、RemoteデータのEditor Panel読込はM6-Aに含まない。

2026-11-15の最終結果: 専用Debug/Release各7 suite、Disabled/MeasurementsDisabled各1 suite、Debug/Releaseの別プロセス5シナリオが成功した。通常Engine Debug/Release x64増分ビルドと両Diagnostics各7 suite、DebugのExplicit Frame/Physics Collision/SoftBody統合、CPU UI Debug回帰、通常Engine実行ファイルによる別プロセス5シナリオも成功。既存Data/Network回帰は終了コード0で、制御Message 429項目、Windows TCP 53項目を含む。未知/不正CLIはGPU初期化前に終了コード1となることを確認した。

既存PDB問題の回避フラグとGLFW native headerのローカル追加参照を使用し、依存設定の恒久変更は行っていない。新規コンパイラWarningは0。既存WarningはEngine Debugのリンク2、Release 0、CPU UIのC4244が32とリンク2、専用/W4のC4324がDebug/Release各6、無効化各3。実通信はサンドボックス外で検証した。実GPU画面・LAN・強制Crash・実ディスク故障注入は未検証で、今回の完了条件に含めない。
