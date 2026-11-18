# Raven UI Modernization Roadmap

更新日: 2026-11-17
対象: `Root/Root/Raven/UI`、Raven Editor / Runtime UI  
状態: **M0〜M3実装・CPU検証済み（M2は監査済み型の明示opt-in、GPU確認はOpenGL基盤の統合ケースに限定）**（本書のPhase M0〜M7は既存ロードマップのPhase 1〜14とは別管理）

## 目的

RE:UIの公開情報から得られた設計思想（MVVC、View-driven Get/Set、共有定義とインスタンス状態の分離、中間描画コマンド、Headless Test）を参考に、Ravenの既存UIを**置き換えずに段階的に改善**する。

目標は、EditorとGame UIで共通のRetained UI基盤を使いながら、計測可能なCPU/GPU負荷削減、明確なModel依存境界、テスト容易性、RHI対応を実現すること。RE:UIの内部実装と同一であることは目標にしない。

## 既存実装と前提

既存の [RAVEN_UI_ROADMAP.md](../proceed/RAVEN_UI_ROADMAP.md) を正とし、未完了項目を完了扱いしない。

- `UIContext`: BeginFrame/EndFrame、入力配送、Capture、Focus、Drag & Drop、Popup/Tooltip、Window別Context、`UIFrameMetrics`。
- `UIElement`: Retained Tree、Measure/Arrange、既存Measure/Arrange Dirty、Transform、Clip、Opacity。
- `UIDrawList`: GPU API非依存のフレーム描画要求。既存方針ではフレームごとに再構築。
- `UIRenderer` / `OpenGLUIRenderer`: 描画抽象境界とOpenGL実装。
- 既存UIロードマップにはDocking、Multi-Viewport、Theme/DPI、Immediate Mode風API、各種Widgetの実装・検証記録がある。
- 未完了・継続検証: Text/IMEの一部、異DPI、他Backend、RHI Batching、UI Debugger/Profiler、正式Editor移行など。詳細は既存ロードマップ参照。
- `RHI_MIGRATION_CHECKLIST.md` に従い、OpenGL UIのテッセレーションとOpenGL状態操作の分離は、RenderTarget/Scissor/Draw offset等のAPI設計を伴う独立作業とする。

## 目標構造

```text
Raven Editor / Runtime
         |
  View / ViewController (MVVC; application layer)
         |
 UIContext / UIElement (existing retained tree)
         |
 Layout / Input / Style / Dirty
         |
 UIDrawList (API-independent commands)
         |
 Geometry Builder / Batching
         |
 UIRenderer
   +-- OpenGL (existing)
   +-- RHI (incremental)
   +-- Headless command comparison (tests)
```

原則:
1. `UIElement`にModelやControllerを強制しない。MVVCはEditor/RuntimeのView層で適用する。
2. 値のPullは安価な表示状態に限定。イベント（Click、Drag、通知）は既存Event Systemを維持。
3. 入力中のText、Sliderドラッグ、Undo/Redo、Selection変更時は双方向同期の競合を防ぐ。
4. DirtyはLayout / Paint / Geometry / Style等の責務ごとに区別。既存Measure/Arrange Dirtyを再利用する。
5. キャッシュでCPUコマンド生成を省いても、GPU描画を省けるとは限らない。Overlay、Clip、Z順、Transform、Window/DPI境界を保証する。
6. Widget Definition/State分離やPoolは、計測で利益が確認できた場合のみ導入。
7. Dear ImGui Editorは代替機能の実動作検証まで維持する。
8. 各Phaseは小さなPRに分割し、コード実装・ビルド・CPUテスト・GPU実描画・ユーザー確認を区別して記録する。

## Phase一覧（新規計画）

| Phase | 期間目安 | テーマ | 完了条件 |
|---|---|---|---|
| M0 | 1〜2週 | Performance Baseline | 100/1,000/10,000 Widgetの再現可能な測定・回帰テスト |
| M1 | 1〜2週 | Dirty拡張 | Layout/Paint/Geometryの無効化規則と回帰テスト |
| M2 | 2〜3週 | DrawList Cache | 静的SubtreeのCPU Build削減を測定で確認 |
| M3 | 2〜3週 | MVVC / View-driven Pull | InspectorのModel操作をController経由にしUndo/編集競合を検証 |
| M4 | 2〜3週 | Shared Definition / State | メモリ・生成コスト比較に基づく限定導入 |
| M5 | 3〜4週 | Geometry / RHI Backend | OpenGLとの描画一致、Clip/Window/DPI回帰 |
| M6 | 1〜2週 | Headless UI Testing | GPU不要の描画コマンドSnapshotテスト |
| M7 | 4〜6週 | Editor/Runtime Toolkit | 代表Editor PanelとGame UIで共通基盤を検証 |

期間は仮見積もりであり、実測と既存PR状況で見直す。M5はRHI移行作業と並行可能。M6の最小版はM0〜M2と並行して先行導入可能。

### M0: Performance Baseline

- [x] `UIFrameMetrics`の既存項目（BuildMilliseconds、DrawCommandCount、Vertex/Index、TextureSwitch、Allocation、ElementCount）を棚卸しする。
- [x] Layout、DrawList生成、Geometry変換、GPU Submit/Drawの時間を可能な範囲で分離する。
- [x] 100 / 1,000 / 10,000 Widgetの静的・頻繁更新・Scrollケースを用意する。
- [x] Release/Debug、Window/DPI、Font、Clip、Popup等の測定条件を固定する。
- [x] Text、DPI、Multi-Viewport等の既存未完了テストを洗い出す。
- 実装・測定結果: [M0 Baseline](../../docs/UI_MODERNIZATION_M0_BASELINE.md)。Debug/Releaseのビルド・CPU回帰成功。OpenGLのFont/Texture・共有Context描画/Resize/復帰はGPU統合で確認。BaselineのGPU時間・異DPI・複数Window性能は未測定。
- **完了条件**: 変更前の測定結果、再実行手順、回帰テストが記録されている。

### M1: Dirty Flag拡張

- [x] `UIElement`のMeasure/Arrange DirtyとInvalidate経路をコード監査する。
- [x] Paint / Geometry / Style（必要ならTransform）の無効化境界を定義する。
- [x] Setterで同値更新を抑制し、親子へのDirty伝播を明文化する。
- [x] Text変更、Opacity、Transform、Clip、Theme、DPI変更の回帰テストを追加する。
- 実装・検証結果: [M1 Dirty](../../docs/UI_MODERNIZATION_M1_DIRTY.md)。Debug/ReleaseのUI回帰・OpenGL GPU統合が成功。同値更新の実Measure削減を確認。DrawListは毎Frame生成を維持し、Cache未監査Widgetの境界を明記。
- **完了条件**: 既存描画・入力結果を維持し、不要な再計算を減らせる。

### M2: DrawList Cache

- [x] `UIDrawList`を維持しつつSubtree単位の描画コマンド再利用方式を検討する。
- [x] 親Transform/Clip、Z順、Popup、Tooltip、Frame Overlay、Texture寿命を考慮する。
- [x] Text Glyph / SVG Tessellationのキャッシュ境界を整理する。
- [x] 静的/動的混在ケースでBuild CPU時間とメモリ使用量を比較する。
- 実装・測定結果: [M2 Cache](../../docs/UI_MODERNIZATION_M2_CACHE.md)。Debug/ReleaseのUI回帰とCache off/onのOpenGL GPU統合成功。Release 10,000静的WidgetでBuild約74.8%削減、Snapshot保持約1.92MB。Text/Texture/SVG/未監査型は通常生成を維持。
- **完了条件**: 描画一致を維持したうえで、静的UIのCPU Build削減を実測できる。

### M3: MVVC / View-driven Pull

- [x] `InspectorRavenPanel`を対象にView/Controller/Modelの依存を整理する。
- [x] 選択EntityのPosition等、安価な値をフレーム単位でPullする。
- [x] `SetValueIfChanged`相当の比較更新を導入する（API名は既存実装に合わせる）。
- [x] UIからのSetはUndo/Redo Commandを経由し、ドラッグ中のCommand統合を考慮する。
- [x] 編集中の値の上書き、Selection変更、Entity破棄、複数選択をテストする。
- 実装・検証結果: [M3 Inspector](../../docs/UI_MODERNIZATION_M3_INSPECTOR.md)。Controller分離、Viewの1更新1Pull、同値比較、既存Command/履歴を維持。Debug/Release UI回帰、Scene寿命回帰、OpenGL基盤統合成功。複数選択は明示拒否、実Inspector手操作は未検証。
- **完了条件**: Inspectorの主要操作がController経由で機能し、従来のUndo/入力動作が維持される。

### M4: Shared Definition / Instance State

- [x] Widgetごとの保持メモリ、生成/破棄頻度、Allocationを計測する。
- [x] Theme/Style等の共有可能な不変データを抽出する。
- [x] Hover/Pressed/Focus/Scroll/Text編集状態はWidgetまたはContextに適切に保持する。
- [x] Pool/固定長Instance管理と現行所有権モデルを比較する。
- 実装・検証結果: [M4 State](../../docs/UI_MODERNIZATION_M4_STATE.md)。Button標準色の共有と個別Overrideの遅延確保を限定採用。Releaseで480→416 bytes、全個Override時の追加確保/コスト増も記録。既存Poolは比較のみでRuntime所有権は維持。Debug/Release UI回帰・Allocation計測・OpenGL統合成功。実Editor操作頻度/異DPIは未測定。
- **完了条件**: 計測で改善した範囲のみ採用。利益がなければ既存構造を維持する。

### M5: Geometry Builder / RHI UI Backend

- [x] `OpenGLUIRenderer`のTessellationとOpenGL State操作を切り分ける。
- [x] API非依存のGeometry/Batch中間表現を設計する。
- [x] RHI RenderTarget、Scissor、UI Draw offset、Texture/Buffer/CommandListの必要機能を整理する。
- [x] Draw順とClipを維持したBatchingを計測する。
- [x] OpenGL/RHIのText、SVG、DPI、Multi-Viewport描画を比較する。
- 実装・検証結果: [M5 Geometry](../../docs/UI_MODERNIZATION_M5_GEOMETRY.md)。共通Geometry/Pixel Clip/隣接Batch、Explicit論理サイズ、UI専用Alpha合成契約を追加。Debug/Release CPU回帰とOpenGL/Vulkan/DX12 GPU比較・複数Window回帰成功。1/2倍はRGBA完全一致、1.25倍は共有辺上のPixel中心一致点を区別して比較。物理Monitor間DPI・全Driverは未検証。
- **完了条件**: 既存OpenGL経路を残し、RHI経路で主要UI描画が一致する。

### M6: Headless UI Testing

- [x] `UIDrawList`のコマンドを正規化・シリアライズするテスト用形式を定義する。
- [x] GPU非依存のExpected Draw Commands / Snapshot差分テストを追加する。
- [x] Layout、Focus、Input、Clip、Popup、Themeの代表ケースをカバーする。
- [x] 浮動小数点誤差、Font Atlas/Texture Handle等の環境依存値を正規化する。
- 実装・再実行手順: [M6 Headless UI](../../docs/UI_MODERNIZATION_M6_HEADLESS.md)。既存CPU UI回帰で描画順と正規化済み項目を比較する。
- **完了条件**: CIで主要UIのコマンド比較をGPUなしで実行できる。

### M7: Raven UI Toolkit

- [ ] Inspector / Hierarchy / PropertyGridの共通部品を整備する。
- [ ] TreeView / Table / Virtualizationを大規模Sceneで検証する。
- [ ] Graph / Timelineの共通基盤を検討する。
- [ ] Runtime HUD/Menu、Gamepad Navigation、UI Animationとの共通化を検証する。
- [x] Dear ImGuiの完全撤去を維持し、Editor／RuntimeのUIをRaven UIで賄う。依存・ビルド設定・実行時切替の再導入がないことを確認する。
- 実装・検証: [M7 Toolkit](../../docs/UI_MODERNIZATION_M7_VIEWPORT.md)。大規模HierarchyとTable、Inspector数値行、HUD／TitleのCPU統合、Graph／Timelineの設計とDear ImGui撤去の確認を記録。実機Gamepadと実GPUでのRuntime操作は未検証。
- **完了条件**: EditorとGame UIの代表例が同一Core/Rendererを利用し、操作・性能テストを通過する。

## 最初のPR（推奨）

**PR 1: UI Performance Baseline**

変更対象候補:
- `UIContext` / `UIFrameMetrics`（既存計測の整理・必要な項目追加）
- UI Performance Demoまたは既存UI Test
- 計測手順・結果ドキュメント

受け入れ基準:
- 既存UIの動作変更なし。
- 100/1,000/10,000 Widgetで同じ条件の計測が再現できる。
- Layout/DrawList/Rendererの計測対象と未計測箇所が明示される。
- Windowsビルド・既存UI回帰テスト・実描画の結果を個別に記録する。

## 関連資料

- [Raven UI実装ロードマップ](RAVEN_UI_ROADMAP.md)
- [RHI移行チェックリスト](RHI_MIGRATION_CHECKLIST.md)
- [RE:UIに関するCAPCOM Open Conference RE:2026取材記事](https://gamebiz.jp/news/433531)
- [RE:UI / REXの技術解説（ゲームメーカーズ）](https://gamemakers.jp/)

> 本書はRavenへの設計提案であり、RE:UIの未公開内部実装を断定するものではない。
