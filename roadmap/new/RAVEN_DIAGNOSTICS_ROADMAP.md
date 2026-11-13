# Raven Diagnostics 実装ロードマップ

## 目的

RE:Logの設計思想を参考に、RavenのLog・Counter・Trace・Profiler・Validationを共通イベント基盤に統合する。RHI、Physics、Renderer、Animationなど既存Subsystemの実装は維持し、診断情報の発行・収集・表示を分離する。

本書は計画であり、記載機能の実装完了を意味しない。

実装状況（2026-11-14）: M1（Phase 01〜03）は実装・CPU検証完了。
詳細契約と検証手順は `Root/Root/Raven/Diagnostics/Tests/README.md` を参照。
M2以降は未着手。CPUProfiler、Backend Validation、既存Assertは維持する。

## 基本設計

```text
Core / RHI / Renderer / Physics / Animation / Asset / Runtime
                         |
                  Diagnostics API
            Log / Counter / Trace / Assert
                         |
                 Thread-local buffers
                         |
                Diagnostic Collector
                         |
              Sink / Transport interface
                  /       |       \
             Console    File     Editor
                                    |
                              Log / Timeline
```

- Subsystemは出力先を知らず、構造化イベントのみ発行する。
- DiagnosticsはCoreに依存できるが、CoreからDiagnosticsへの循環依存は避ける。
- Diagnostics（時系列イベント）とFollows（依存関係グラフ）は独立し、IDで関連付ける。
- Hot Pathでは文字列整形、ファイルI/O、ネットワーク送信を避ける。
- イベントID・Payloadの型と寿命、Buffer Overflow、Shutdown Flush、Crash Fallbackを早期に設計する。
- 既存Loggerの調査と移行計画を先行し、二重実装を長期化させない。

## Phase 01 — Diagnostics Core（目安3〜5日）

- [x] 既存のログ・Assert・Validation経路を調査する
- [x] `DiagnosticEventId`、`DiagnosticCategory`、`DiagnosticSeverity` を定義する
- [x] Timestamp、Thread ID、Sequence Numberを含むイベントヘッダを設計する
- [x] 型付きPayloadとデータ所有権・サイズ上限を定義する
- [x] `RAVEN_LOG_INFO/WARN/ERROR` などのAPIを用意する
- [x] 最初は同期Console Sinkで動作を検証する

**完了条件:** RHIとPhysicsから同じAPIでログを発行でき、イベントIDとPayloadを保持できる。

## Phase 02 — Event Buffer / Collector（目安5〜8日）

- [x] スレッドごとのSPSC Ring Bufferを設計する
- [x] Producer/Consumerのメモリ順序とスレッド登録・破棄を定義する
- [x] Collector Workerを実装する
- [x] Overflow PolicyとDropped Event Counterを設ける
- [x] Flush、Shutdown、Crash時のFallbackを検証する（通常終了と独立stderr経路。実Crash CaptureはPhase 08）
- [x] 複数スレッド負荷テストを実施する

**完了条件:** 並列ログ発行でデータ破損がなく、取りこぼしを検出できる。

## Phase 03 — Output / Transport（目安3〜5日）

- [x] `IDiagnosticSink` とSinkの所有権を設計する
- [x] ConsoleSink、FileSinkを実装する
- [x] バージョン付きBinary Trace Formatを定義する
- [x] Event IDとPayloadのシリアライズ・再読込を実装する
- [x] Category/Severity Filterを設ける
- [x] I/O障害と終了時Flushを検証する

**完了条件:** ConsoleとFileへ独立出力でき、保存したイベントを再読込できる。

**M1: Diagnostics Logger v1（Phase 01〜03）**

### M1実装結果（2026-11-14）

- 既存のAPI / SPSC / CollectorとRHI・Physics接続を維持し、CollectorからConsole / File / Binary Trace Sinkへ分離した。
- SinkはCollectorが所有し、Category / Severity Filterと故障時の切り離しを行う。故障数はprocess通算で観測可能。
- Binary Trace v1.0はlittle endian・固定型タグ・上限付きrecord。全Payload型とHeaderを保存・復元し、不正長・未知Version・途中切断を拒否する。
- Thread IDのprocess内採番、終了ThreadのBuffer回収、通常ApplicationのRAII開始・終了、ビルド時／実行時のログ無効化を追加した。
- Debug / Release専用MSVC Runnerは各2 suite成功。Disabled構成は1 suite成功。4 Thread・8192発行の保存再読込で受信数＋Drop数の整合性とProducerごとのFIFOを確認した。
- Engine全体のDebug / Release（x64）はビルド成功し、それぞれの実行ファイルでもDiagnostics 2 suite成功。DebugのExplicit Frame / Physics Collision回帰も成功した。
- 通常Debugビルドは環境の`MSPDB140.DLL`版不一致（LNK1101）でリンク失敗した。既存設定`RavenGenerateLinkDebugInformation=false`と`LinkIncremental=false`を指定して再確認した。プロジェクトの既定設定は変更していない。
- 全体ビルドWarningはDebug 2件（LNK4075 / LNK4098）、Release 0件。専用Runnerの`/W4`では既存の`alignas(64)`構成に伴うC4324がDebug / Release各5件、Disabled 3件発生した。
- 無効化時の概算測定はReleaseで約1.02 ns/call（単純ループ約0.20 ns）、ビルド時無効化で約0.20 ns/call（単純ループ約0.20 ns）。実行条件に依存する参考値。

未検証: GPU実機での通常起動・終了とValidation故障注入、実Crash時の全件保存。
制約: Emitは共通制御mutexと値所有stringを使用し、lock-free / allocation-freeではない。
次の候補はM2（Phase 04）のCounter / Traceと既存CPUProfilerの橋渡し。開始は別承認とする。

## Phase 04 — Counter / Trace（目安5〜8日）

- [ ] CounterイベントとFrame IDを追加する
- [ ] `RAVEN_COUNTER` APIを追加する
- [ ] RAII方式の `RAVEN_TRACE_SCOPE` を追加する
- [ ] Trace Begin/Endとスレッド別Timelineを再構築する
- [ ] 無効化時・有効化時のオーバーヘッドを測定する

**完了条件:** PhysicsStepやRHI SubmitのCPU時間とCounterをフレーム単位で確認できる。

## Phase 05 — Subsystem Integration（目安5〜10日）

- [ ] RHI BackendのValidationを共通イベントに変換する
- [ ] Physics BroadPhase / NarrowPhase / Solver Counterを接続する
- [ ] SoftBody Candidate / Constraint / Correction Counterを接続する
- [ ] Renderer DrawCall / Resource Counterを接続する
- [ ] Subsystem単位の有効・無効設定と回帰テストを整備する

**完了条件:** RHIとPhysicsの診断情報を共通フォーマットで収集できる。

## Phase 06 — Editor Diagnostics（目安7〜12日）

- [ ] Log Table、Search、Severity/Category Filter
- [ ] Frame/Thread別FilterとEvent詳細Inspector
- [ ] Counter GraphとCPU Trace Timeline
- [ ] 大量イベント時の表示性能・メモリ使用量を検証する

**完了条件:** 特定フレームのRHIエラーとPhysics処理時間を同じEditorで確認できる。

## Phase 07 — Structured Message Database（目安5〜8日）

- [ ] 安定したMessage IDと型付き引数Schemaを定義する
- [ ] ビルド時のMessage Metadata生成を設計する
- [ ] 実行時の文字列整形とMessage Dictionaryを分離する
- [ ] 日本語/英語切替とID衝突検査を実装する
- [ ] DictionaryとBinary Traceのバージョン互換性を検証する

**完了条件:** 同一Binary Eventを複数言語で表示できる。

## Phase 08 — Advanced Diagnostics（必要時）

- [ ] NetworkSinkとRemote Collector
- [ ] GPU Timestamp QueryとCPU/GPU Timeline相関
- [ ] Crash Capture / Replay補助
- [ ] Follows Node IDとの連携
- [ ] 長期保存・検索

**完了条件:** 別プロセスから診断情報を確認でき、関連イベントを追跡できる。

## マイルストーン

| Milestone | Phase | 成果物 | 検証 |
| --- | --- | --- | --- |
| M1 | 01〜03 | 非同期Logger v1 | 並列性、Flush、Overflow、再読込 |
| M2 | 04 | CPU計測基盤 | Trace精度、オーバーヘッド |
| M3 | 05 | RHI/Physics統合 | 既存挙動の回帰 |
| M4 | 06 | Editor Viewer | 大量イベント表示 |
| M5 | 07 | Message Database | ID・Schema互換性 |
| M6 | 08 | Remote Diagnostics | 通信断・復旧・記録 |

## APIイメージ（確定仕様ではない）

```cpp
RAVEN_LOG_ERROR(DiagnosticCategory::RHI,
               EventIds::LegacyRHIInitializationFailed,
               std::string("Backend unavailable"));
RAVEN_LOG_INFO(DiagnosticCategory::Physics,
               EventIds::PhysicsGravityUpdated,
               std::string("Physics gravity updated"));

RAVEN_TRACE_SCOPE("PhysicsStep");
RAVEN_COUNTER("Physics.NarrowPhase.Candidates", candidateCount);
```

初期APIで文字列フォーマットを使用する場合も、内部イベントID・型付きPayloadとの対応を明示する。Trace/Counterはビルド設定によって無効化できるようにする。

M1で確定したLog APIは `category, event ID, owned payload` の3引数。
上記Log部分は実装済みAPIの呼出形式、Trace/Counter部分はM2の未実装イメージ。
Payloadは単一の `int64 / double / bool / string` とし、文字列は最大1024 byteを保持する。
複合引数・書式文字列・Message Dictionaryの生成はM5で設計する。

## 推奨する責務分割（構成案）

```text
Diagnostics/
  Public/
    DiagnosticEvent.h
    DiagnosticId.h
    DiagnosticCategory.h
    DiagnosticAPI.h
    Counter.h
    TraceScope.h
  Private/
    EventBuffer.cpp
    Collector.cpp
    DiagnosticSystem.cpp
    MessageRegistry.cpp
  Sinks/
    ConsoleSink.cpp
    FileSink.cpp
    BinaryTraceSink.cpp
  Tests/
```

実際の配置はRaven既存構成・命名規約・ビルド定義を確認して決定する。新しいVisual Studio管理ファイルを追加する場合はfiltersも更新する。

## 導入優先順位と留意点

1. Phase 01〜03で最小の構造化非同期Loggerを完成させる。
2. Phase 04〜05で既存Physics CounterとRHI Validationを段階移行する。
3. Phase 06〜07でEditor可視化とMessage Databaseを整備する。
4. Phase 08は実需に応じて実装する。

各段階でDebug/Releaseを確認し、診断無効時の実行コストも測定する。ロードマップ文書の追加のみではビルド・実行テストは不要だが、実装時には関連ビルドとテストを行う。
