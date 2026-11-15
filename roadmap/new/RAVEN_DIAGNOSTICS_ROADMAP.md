# Raven Diagnostics 実装ロードマップ

## 目的

RE:Logの設計思想を参考に、RavenのLog・Counter・Trace・Profiler・Validationを共通イベント基盤に統合する。RHI、Physics、Renderer、Animationなど既存Subsystemの実装は維持し、診断情報の発行・収集・表示を分離する。

本書は計画であり、記載機能の実装完了を意味しない。

実装状況（2026-11-15）: M1（Phase 01〜03）、M2（Phase 04）、M3（Phase 05）、M4（Phase 06）、M5（Phase 07）は実装・CPU検証完了。
詳細契約と検証手順は `Root/Root/Raven/Diagnostics/Tests/README.md` を参照。
M6-A（Windows Loopback Remote Diagnostics）は実装・CPU/別プロセス検証完了。M6の残りは未着手。CPUProfilerのsnapshot、Native Debug Layerと既存Assertは維持する。

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

- [x] CounterイベントとFrame IDを追加する
- [x] `RAVEN_COUNTER` APIを追加する
- [x] RAII方式の `RAVEN_TRACE_SCOPE` を追加する
- [x] Trace Begin/Endとスレッド別Timelineを再構築する
- [x] 無効化時・有効化時のオーバーヘッドを測定する

**完了条件:** PhysicsStepやRHI SubmitのCPU時間とCounterをフレーム単位で確認できる。

### M2実装結果（2026-11-14）

- 固定長の計測Payload、Frame ID、Scope/Parent Scope IDを追加した。名前はUTF-8境界を保って96 byteまで保持し、Counterは有限のdoubleとする。
- 計測は既定で無効。`RAVEN_COUNTER` / `RAVEN_TRACE_SCOPE`を追加し、実行時・ビルド時の無効化で名前・値の式を評価しない。Logを維持して計測だけ無効化する構成も用意した。
- RendererがFrame IDを採番し、既存CPUProfilerにも渡す。CoreからDiagnosticsへの依存は追加せず、`ProfiledTraceScope` Adapterで試験箇所だけを両方へ記録する。
- Physics StepとManifold数、RHI SubmitのCPU区間と成功値だけを試験接続した。PresentやGPU完了時間とは分離し、既存の失敗伝播を維持した。
- Scopeは開始Frameへ帰属させる。開始後の無効化でも同じSession内のEndを閉じ、再起動後には旧SessionのEndや親IDを混入させない。
- オフラインTimeline再構築は到着順に依存せず、片側欠落・親欠落・重複・metadata不一致・時刻逆転を報告する。上限を超える入力は確保前に拒否する。
- Binary Trace v1.1はtag 5の計測Payloadを追加し、v1.0のLogも読める。旧Readerは未知minorを拒否する。
- 通常Applicationの`--diagnostics-measurements`でConsole計測、`--diagnostics-trace=path.rvndiag`でBinary記録を有効化できる。保存時のConsoleはLogだけを表示する。
- 専用MSVC RunnerとCIはDebug / Release / Disabled / MeasurementsDisabledの4構成を扱う。詳細契約はDiagnosticsのTests/READMEを参照。

検証結果:

- 専用Runner: Debug / Releaseは各3 suite成功、Disabled / MeasurementsDisabledは各1 suite成功。M1の保存・並列性検証も継続して実行した。
- Engine全体のDebug / Release x64ビルド成功。両実行ファイルのDiagnostics各3 suite、DebugのExplicit Frame / Physics Collision回帰が成功した。
- Explicit FrameのMockでRHI Submitの区間・成功/失敗値とPresent順序を確認し、空Sceneの実Physics Stepでも区間とManifold数の発行を確認した。
- CPU UI Debug回帰も終了コード0。CPUProfilerの表示用snapshotを扱う既存検証を維持した。
- 全体ビルドは既存のPDB問題を避け、`RavenGenerateLinkDebugInformation=false` / `LinkIncremental=false`で確認した。UI回帰はローカルで不足していたGLFW 3.4のnative headerを`artifacts/sg3-deps`のキャッシュから一時propsで追加参照した。いずれもプロジェクトの依存・既定設定は変更していない。
- Warningは全体Debug 2件（LNK4075 / LNK4098）、Release 0件。専用/W4は既存のalignas構成に伴うC4324がDebug/Release各6件、無効化構成各3件。UI再ビルドは未変更箇所のC4244が32件とリンクWarning 2件で、変更した診断コードの新規コンパイラWarningはなかった。
- ReleaseのNull Sink測定例はCounter約127 ns/call、Trace Scope約236 ns/区間（2イベント）、実行時無効のCounter+Scope約7.2 ns/組。登録・batch間Flushを測定区間から除外し、Drop=0を確認した。値は環境依存の参考値。

注意: Begin/Endが両方消えたScopeはイベントだけでは検出できないため、取得時のDrop差分を再構築へ渡す。Drop差分はファイルへ自動保存しない。
初回Thread登録と共通制御mutexは残る。M3の全面接続、M4のEditor Viewer、GPU実機の時間検証は対象外。
次の候補はM3（Phase 05）のSubsystem Integration。開始は別承認とする。

## Phase 05 — Subsystem Integration（目安5〜10日）

- [x] RHI BackendのValidationを共通イベントに変換する
- [x] Physics BroadPhase / NarrowPhase / Solver Counterを接続する
- [x] SoftBody Candidate / Constraint / Correction Counterを接続する
- [x] Renderer DrawCall / Resource Counterを接続する
- [x] Subsystem単位の有効・無効設定と回帰テストを整備する

**完了条件:** RHIとPhysicsの診断情報を共通フォーマットで収集できる。

### M3実装結果（2026-11-14）

- Vulkan callback / DX12 InfoQueueを、Backend・元ID・Native Severity/Type・観測時Frame・所有する名前/本文・切詰めフラグを持つValidation Payloadへ変換した。既存のVerbose条件とcallback戻り値・Queue Clear・Live Objects reportは維持した。
- Callbackでは診断の制御・登録mutexを待たない。停止中・競合・Queue満杯・再入・確保失敗ではstderrへ退避する。退避回数は独立APIで観測し、Native ABIへC++例外を返さない。
- PhysicsはBroadPhase候補、有効なNarrowPhase候補、Plane判定仕事量、Solver既存統計をループ外で発行する。既存Profiler名とM2 Manifold Counterを維持し、同じ値を重複発行しない。
- SoftBodyはPhysicsと独立したcategory値7を追加した。Step末尾でTopologyとParticle-Triangle統計を発行し、詳細未収集はDetailedCollected=0とする。詳細収集設定を自動変更せず、Constraint/Correctionは収集時だけ発行する。
- RendererはFrame guardで正常・失敗終了時の既存描画統計を発行し、同じSession/Frameでの二重計上を防ぐ。ResourceはRHIResourceの生存C++ラッパー数とし、所有参照やGPU操作を追加しない。
- Category別のatomicな発行設定と通常Applicationの`--diagnostics-disable=rhi,physics,softbody,renderer`を追加した。設定は共通発行だけに作用し、計算・既存Profiler・Native Debug Layerを変更しない。
- Binary Trace v1.2はValidation tag 6とSoftBody categoryを追加した。新Readerはv1.0/1.1も読み、旧Headerへの新型混入、長さ・Backend・Flags・Reservedの不正を拒否する。
- 専用RunnerはDebug/Release各4 suiteと無効化2構成を扱う。CIへ実EngineのDiagnostics・Subsystem Mock・Physics・SoftBody統合回帰も追加した。

検証: 専用4構成、Engine Debug/Release x64とDiagnostics、Debugの実Subsystem比較・Physics Collision・SoftBody統合、CPU UI Debug回帰が成功した。実統計との一致、同一入力の診断有効/無効での結果一致、詳細未収集の区別、Renderer重複発行防止、Native callbackの停止・Flush経路を確認した。
全体ビルドは従来のPDB無効化とキャッシュ済みGLFW 3.4 native headerの一時参照で確認し、依存設定は変更していない。Debugは既存リンクWarning 2件、Releaseは0件。GPU実機の故障注入は未実施。

検証・未検証事項の詳細は `Root/Root/Raven/Diagnostics/Tests/README.md` を参照。
Resource数はGPU handle数・使用byte・Fence完了・リークの証明ではない。ValidationのFrameはCPUで観測した番号で、GPUの原因Frameを保証しない。
stderrへ退避したValidationはBinaryへ自動保存しない。共通mutex・初回Buffer確保、GPU実機の故障注入・終了確認は残る制約。
次の候補はM4（Phase 06）のEditor Diagnostics。開始は別承認とする。

## Phase 06 — Editor Diagnostics（目安7〜12日）

- [x] Log Table、Search、Severity/Category Filter
- [x] Frame/Thread別FilterとEvent詳細Inspector
- [x] Counter GraphとCPU Trace Timeline
- [x] 大量イベント時の表示性能・メモリ使用量を検証する

**完了条件:** 特定フレームのRHIエラーとPhysics処理時間を同じEditorで確認できる。

### M4実装結果（2026-11-15）

Editor Sink → 容量制限付きStore → Main Threadの所有Snapshot → Diagnostics Panelを接続した。
Log Table、本文検索、Severity/Category/Frame/Thread Filter、詳細Inspector、Counter Graph、Thread別CPU Timelineを提供する。
既定Dockへ安定ID 1007を追加し、旧保存配置を維持する。旧配置ではView > Diagnosticsで表示する。

Storeは既定8192件・推定8 MiBの保持予算で古いイベントを破棄する。UI Snapshotは最大5 Hz、Freeze中も収集は継続する。
Frame不明の汎用Logは`?`、Frame 0は既知の起動時として区別し、Search/Severityで同FrameのPhysics Traceを消さない。
Queue Drop、Store Eviction/Reject、Native stderr fallback、不完全Scope、詳細Counterの未収集を区別する。
Counterは最大512個の生サンプル、Timelineは最大2048区間/32 Threadの表示とし、深い階層の完全な展開・Zoomは提供しない。

Debug/Releaseの診断5 suite、診断無効化2構成、Engine両構成、実Subsystem比較・Physics Collision・SoftBody統合、CPU UI回帰が成功した。
既定Storeへの10万件投入・Snapshot・Model更新は専用Releaseで約7.9 ms、保持推定1.56 MiB。8192イベント/40 ThreadのCPU UI更新・Chart構築はDebugで約60 msだった。
値はローカルの単発測定で、GPU描画時間や60 FPSの保証ではない。GPU画面の起動確認は操作可能なWindowを取得できず未検証。
既存PDB/GLFW検証用回避を使用し、依存設定の変更は行っていない。Snapshotコピーと再構築の負荷、深いScope階層の視認性は残る制約。
次の候補はM5（Phase 07）のStructured Message Database。開始は別承認とする。

## Phase 07 — Structured Message Database（目安5〜8日）

- [x] 安定したMessage IDと型付き引数Schemaを定義する
- [x] ビルド時のMessage Metadata生成を設計する
- [x] 実行時の文字列整形とMessage Dictionaryを分離する
- [x] 日本語/英語切替とID衝突検査を実装する
- [x] DictionaryとBinary Traceのバージョン互換性を検証する

**完了条件:** 同一Binary Eventを複数言語で表示できる。

### M5実装結果（2026-11-15）

Message Catalog/ID・Schema履歴から型付きFactoryと日英Metadataを生成し、同じ所有引数を表示時に翻訳する。
既存IDのPhysics重力/RHI初期化失敗を移行し、旧scalar/string/Native Validation/Metricは維持する。
Binary v1.3のtag 7にDictionary版・Schema版/hash・型付き引数を保存する。v1.0〜1.2を引き続き読み、未知Dictionary/Schemaは生値へfallbackする。
EditorのLanguageで表示と本文検索を切り替え、Snapshotは書き換えない。日本語Templateの必要文字だけを起動時Atlasへ渡し、Windowsでは利用可能ならMeiryoを使う。

生成器はPowerShell 5.1/7で再現可能。通常ビルドは生成済みMetadataの陳腐化/衝突を検査し、Schema履歴の変更は明示更新とする。
文字列・引数数・合計byte・Template置換数を制限する。深いコピーを含めStoreのbyte予算へ反映する。
未知Schemaの自動移行、過去全版のDictionary配布、任意引数の全Unicode Glyph対応、Editor全体の翻訳は対象外。
Debug/Releaseの診断各6 suite、無効化2構成、通常Engine両構成、実Subsystem回帰、CPU UIが成功。生成器16項目をPowerShell 5.1/7で確認し、MeiryoのGlyph収録もCPUで確認した。GPU上の日本語描画・Dock操作は未検証。既存PDB/GLFWの検証用回避を使用し、新規Warningはない。
次の候補はM6（Phase 08）のAdvanced Diagnostics。実需に応じた範囲の整理と別承認を必要とする。

## Phase 08 — Advanced Diagnostics（必要時）

- [x] NetworkSinkとRemote Collector（M6-A: Windows Loopback・1 Sessionに限定）
- [ ] GPU Timestamp QueryとCPU/GPU Timeline相関
- [ ] Crash Capture / Replay補助
- [ ] Follows Node IDとの連携
- [ ] 長期保存・検索

**完了条件:** 別プロセスから診断情報を確認でき、関連イベントを追跡できる。

### M6-A実装結果（2026-11-15）

Network Sink→容量制限付きQueue→専用通信Worker→別プロセスのHeadless受信モードを追加した。
既存Windows Loopback TCPを再利用し、制御Codec依存のAdapterだけを分離した。ローカルSinkは通信完了を待たない。
Protocol v1はSession/Connection/OrdinalとDrop値を持ち、Binary v1.3のEventを転送する。切断時に接続を閉じて再試行し、部分イベントを再送しない。
受信は1 Sessionに固定し、Binaryと`.remote.json`へ保存する。既存ファイルは原子的に上書き拒否し、保存量・Frame受信期限・全体期限を制限する。
送信OS受付、受信/保存、Queue Drop、process Producer Drop、中断/拒否、正常Closeを区別する。ACKや保存保証は提供しない。

診断Debug/Release各7 suite、無効化2構成、Debug/Release別プロセス各5シナリオ、通常Engine両構成と既存Subsystem/CPU UI回帰が成功。既存制御Message 429項目とWindows TCP 53項目も成功。Loopbackは実行サンドボックスの通信制限外で検証した。既存PDB/GLFWの検証用回避を使用し、新規Warningはない。

M6-Aの範囲は上記Remote Diagnosticsのみ。GPU Timestamp、Crash Capture、Node連携、長期保存/検索、LAN接続、Remote Editor読込は後続の別承認とする。

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
上記Log部分とTrace/Counter部分は実装済みAPIの呼出形式。
Log Payloadは単一の `int64 / double / bool / string` とし、文字列は最大1024 byteを保持する。M2のCounter/Traceは別の固定長計測Payloadを使用する。
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
