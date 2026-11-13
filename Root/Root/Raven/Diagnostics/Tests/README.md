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
| `VulkanInstance.cpp`のValidation callback | `std::cerr`を維持しM3で移行 |
| `DX12Device.cpp`のInfoQueue / Live Objects | `std::cerr`を維持しM3で移行 |
| `MemoryTracker.cpp`等の`assert` | Safety Checkを維持。Assert停止動作の変更は含めない |
| `CPUProfiler`とStatisticsPanel、Physics Counter | 現行frame snapshotを維持し、M2以降で橋渡しを設計 |
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
- Payloadは`int64 / double / bool / std::string`の単一値。文字列は1024 byteまで保持し、正しいUTF-8入力を途中の継続byteで切断しない。数値・文字列の複合引数schemaはM5で扱う。
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

Debug / Releaseとも例外ベースのチェックで同じ2 suiteを実行する。`NDEBUG`でも発行・検証を省略しない。
Console書式、所有Payload、UTF-8境界、SPSC並行FIFO、Overflow、Collector再起動禁止、API再起動、4 Thread負荷とDrop整合性、終了Buffer回収、複数Sink、Filter、通常終了Flush、I/O失敗・例外、Binary全型のround-trip・固定byte・全位置の途中切断・不正入力を検証する。

Disabled構成はPayloadの副作用が消えることと、APIの無効状態を検証する。各構成で単純ループと無効化ログ付きループの概算ns/callも記録する。microbenchmark値には実行環境や最適化の影響があり、性能保証には使わない。
ログは`artifacts/diagnostics/<Configuration>/`。故障注入時の4行のstderrは期待されるfallback出力。

Engine全体は既存の`./scripts/build-windows.ps1 -Configuration Debug -SkipBootstrap`（Releaseも同様）でビルドし、`Root/x64/<Configuration>/Root.exe --test-diagnostics`で同じCPUテストを実行できる。

未検証のRuntime確認は、OpenGL / DX12 / Vulkan通常起動からの正常終了、Scene破棄中のログ保持、実機Validationエラー時の既存stderr動作。M1ではBackend Validationの全面移行やGPU障害注入は行わない。

## 残る制約

M1のEmitは登録・Start/Stopの安全性を優先してprocess共通mutexを取得し、string PayloadはHeap確保を伴い得る。lock-free / allocation-freeなHot Pathは保証しない。Trace / Counterへ展開するM2では専用の固定長Payloadと発行コストを評価する。
Buffer数は同時に生存するProducer数に比例する。SinkのI/Oが無期限に停止するとFlush/Stopも待機するため、任意のTransportへのtimeout・切断復旧はM6で設計する。
