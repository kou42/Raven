# Raven UI 実装ロードマップ

最終更新: 2026-09-21  
対象: `Root/Root/Raven/UI` / Raven Editor  
状態: Phase 1 実装中（Glyph描画入口を追加、Font Atlasは未実装）

## 目的と原則

Raven独自のRetained Mode UI Treeを維持し、Dear ImGui相当のEditor操作性を段階的に獲得する。既存Dear ImGui Editorは独自UIへの移行が検証できるまで維持する。

- UIElement / UIContextは入力・レイアウト・UIの寿命を管理する。
- UIDrawListはフレーム単位の描画要求を保持する。GPU API固有値を入れない。
- UIRendererは描画API境界を担う。RHI移行中の既存経路を壊さない。
- 各Phaseは小さなPRに分割し、実装・テスト・ビルド結果を記録する。
- 「実装済み」はコード確認、「検証済み」は実際のテストまたはビルド確認を意味する。計画を完了扱いにしない。

## 現状の基盤（2026-09-21 コード確認）

- UIContext: BeginFrame / EndFrame、Mouse Routing、Capture、Keyboard Focus、Tab移動。
- UIElement: Retained Tree、Measure / Arrange、Absolute / Vertical / Horizontal、Transform、Clip、Opacity。
- UIDrawList: SolidRect / SolidCircle / SolidPolygon / Image。Text Commandは未実装。
- UIRenderer: API非依存の境界。OpenGLUIRendererは既存実装。
- Dear ImGui: EditorLayerでDockSpaceを利用。独自UI側のDockingとは別。

## 順序と進捗

| Phase | テーマ | 状態 | 完了条件 |
| --- | --- | --- | --- |
| 1 | Font Atlas / Text Rendering | 実装中 | Font読込、Glyph Atlas、DrawList経由の文字表示、日本語fallback、基本描画検証 |
| 2 | Text Layout / Text Measurement | 未着手 | Measure/Arrangeと文字サイズ連携、改行・配置・Clip検証 |
| 3 | InputText / InputNumber | 未着手 | Cursor、選択、編集、Clipboard、Undo/Redo、IME方針と検証 |
| 4 | ComboBox / Popup / Tooltip | 未着手 | Focus、閉じる条件、重なり順、入力伝播の検証 |
| 5 | TreeView / Table | 未着手 | Hierarchy相当の選択・展開、表の列・Scroll・基本操作 |
| 6 | Window System | 未着手 | 論理UI Windowの移動・Resize・Focus・Z順 |
| 7 | Drag & Drop | 未着手 | Payload、Capture、Drop target、Cancel |
| 8 | Tab System | 未着手 | Tab選択・追加・削除・移動 |
| 9 | Docking System | 未着手 | Split / Tab / Dock preview / Layout保存復元 |
| 10 | Theme / Style | 未着手 | 共通Style、状態別外観、DPI |
| 11 | Immediate Mode風API | 未着手 | 安定IDとRetained Element再利用、Debug UI検証 |
| 12 | Multi-Viewport | 未着手 | OS WindowごとのContext、入力、描画Target |
| 13 | RHI Batching最適化 | 未着手 | Draw順・Clipを維持したBatchingと計測 |
| 14 | UI Debugger / Profiler | 未着手 | Tree / Focus / Draw Command / CPU・GPU指標表示 |

## Phase 1: 実装分割

1. [ ] 既存TextureAsset / Texture生成API、Shader、OpenGLUIRenderer、Visual Studio project設定、Fontライブラリの利用条件を確認し、Atlasの所有権・更新方法を決定する。
2. [x] Glyph Metrics / Font Atlasの最小Runtime構造を実装する。Font読込・Rasterize・Texture再生成は未実装。
3. [ ] UTF-8 decode、代替Glyph、ASCII・日本語のGlyph取得を実装する。不正UTF-8と未収録文字をテストする。
4. [x] DrawListへGlyph Quadの入口を追加し、既存Image Commandを再利用する方針を決定する。Font Atlasからの呼び出し・描画検証は未実施。
5. [ ] OpenGLUIRendererでAtlas描画を接続する。既存矩形/画像描画とScissor/Transformを回帰確認する。
6. [ ] UILabelまたは最小Text検証UIを追加し、ASCII・日本語・複数行・Resize・高DPIを確認する。

### Phase 1で注意する点

- ImGui vendor内部のFont Atlasを独自UIの恒久依存にしない。採用するライブラリのライセンスを確認する。
- AtlasのTextureはDrawListの描画完了まで有効にする。GPU Texture IDをUI Coreに持ち込まない。
- 現在のOpenGLUIRendererはメインWindowのdefault framebufferを描画先にしている。別RenderTarget対応は別Phaseで整理する。
- Text表示が成功しても、Text Measurement / InputText完成とは扱わない。

## 作業記録

| 日付 | ブランチ / PR | 変更 | 検証 | 次の作業 |
| --- | --- | --- | --- | --- |
| 2026-09-21 | feature/raven-ui-roadmap | ロードマップとPhase 1の分割・完了条件を作成 | 文書のみ | Phase 1-1の依存関係確認、Font Atlas設計 |\n| 2026-09-21 | feature/raven-ui-roadmap | UIDrawList::AddGlyphを追加。TextureAssetとImage Commandを再利用 | コードレビューのみ。ビルド・実行未検証 | Font Atlas / Glyph Metrics、Font読込とUTF-8対応 |\n| 2026-09-21 | feature/raven-ui-roadmap | UIFontAtlasにGlyph Metrics登録・Atlas範囲検証・UV変換・DrawList接続を追加 | ビルド・実行未検証 | Font Loader、Rasterizer、Texture生成、UTF-8 |

## 更新ルール

各PRで該当Phaseのチェックリスト、状態、作業記録、未検証項目を更新する。未着手 → 実装中 → 実装済み（未検証） → 検証済みの順で記録し、後から問題が見つかれば状態を戻す。Phaseの完了は完了条件を満たした場合のみとする。
