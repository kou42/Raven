# Raven UI Phase 2 回帰テスト棚卸し

確認日: 2026-09-23  
対象: `master` の `Root/Root/Raven/UI`、`Root/Tests/UITextReflowTests.cpp`  
目的: Phase 2「Text Layout / Text Measurement」の**実装の存在**、**テストコードの存在**、**実行・実描画の検証**を混同せずに整理する。

## 現状の結論

- `UITextLayout::Build` は改行、Character / ASCII Word Wrap、Left / Center / Right、GlyphScale、Fallbackを実装している。
- `UITextMeasurement::Measure` は `UITextLayout::Build(...).Metrics` を返す。描画側の `UIFontAtlas::AppendText` も同じLayoutを利用する。
- `UILabel` は `OnMeasureContent` / `OnMeasureContentForWidth` から幅依存の測定を呼び、描画時には確定したWidget幅でLayoutを構築する。
- `UIElement` の幅依存再Measureの回帰テストは存在するが、`WrappingElement` による**仮想テキスト**を使用しており、実際の `UILabel` とFont Atlasを通る統合テストではない。
- `UITextReflowTests.cpp` にはDPI時のLabel Typography / GlyphScale / Font Cacheのテストがある。一方、同ファイル内の `UITextLayout::Build` 直接呼び出しは不正GlyphScaleの拒否確認であり、通常の文字配置・測定・折り返し・配置結果を直接検証する網羅的なテストではない。
- ここでの「テストあり」は**テストコードを確認した**という意味。今回、RavenUITestのビルド・実行、GPU描画、異DPIモニターでの操作は行っていない。

## 回帰テストマトリクス

| 領域 | コード上の実装 | 既存テストで確認できる範囲 | 追加すべき検証 |
| --- | --- | --- | --- |
| UTF-8 / Fallback | DecodeNext、ReplacementCharacter → '?' | 入力編集系のUTF-8テストは存在 | Layout結果のGlyph Codepoint・Advanceを直接確認。不正UTF-8、Atlas未収録、代替Glyph不在 |
| 明示改行 | CRを無視、LFで行追加、末尾空行を保持 | Layout固有の期待値テストは未確認 | 空文字、単一行、CRLF、連続LF、末尾LF、LineCount / Height / FinalPen |
| Measurement | MetricsのWidth / Height / Ascent / Descent | 直接の期待値テストは未確認 | 固定MetricsのAtlasで幅・行高・Ascent / Descentを照合し、描画Layoutと一致させる |
| Character Wrap | MaxWidthとAdvanceで改行 | Layout固有の期待値テストは未確認 | 境界ぴったり、1px不足、幅より大きい1文字、幅制限なし |
| Word Wrap | ASCII単語先読み、日本語は文字単位 | Layout固有の期待値テストは未確認 | 単語境界、長い単語、行末空白、連続空白、ASCII＋日本語混在 |
| Alignment | Left / Center / Rightで行ごとにPenを移動 | Layout固有の期待値テストは未確認 | 行ごとのGlyph Pen、MaxWidth指定時と未指定時、空行、FinalPen |
| GlyphScale / DPI | Layout倍率、UILabelのDPI補正 | `TestDPIGlyphScale`、`TestDPILabelTypographyMetrics` 等 | 実Glyphの測定値と描画位置、DPI変更前後の同一文字列比較 |
| UIElement Measure / Arrange | 幅依存再Measure、親・兄弟への反映 | `WrappingElement` を用いた幅変更・Margin・非表示等のテスト | `UILabel` ＋テスト用Atlasで親Resize→行数/高さ→Sibling位置を確認 |
| Clip | UIElement / DrawListのClip経路 | 一般的なClip矩形のテストは存在 | Glyph Quadが親/子Clipと交差する場合、Wrap・Alignment・Transform併用 |
| 実描画 | AppendText→Glyph Quad→DrawList→Renderer | コード接続は確認 | ASCII・日本語・改行・狭幅・Clip・Resizeを実画面で確認 |

「未確認」は未実装やテスト失敗を意味しない。上記の対象ファイルを調べた範囲で、該当する**直接的な期待値検証を確認できなかった**という意味。

## 推奨する実装順

1. **純CPUのLayout / Measurementテスト**: テスト用の小さなFont Atlasと固定Advanceを使い、Metrics・Glyph Pen・Lines・FinalPenを数値比較する。既存の `UITextReflowTests.cpp` へ関数を追加するか、独立したテストソースを既存RavenUITestターゲットへ登録する。
2. **UILabel / UIElement統合テスト**: 実UILabelのMeasureとArrangeを通し、親幅変更・Stretch・Margin・Sibling位置・Visibilityを確認する。仮想 `WrappingElement` の既存テストは残す。
3. **DrawList / ClipのCPUテスト**: Glyph QuadとClip矩形の交差、Transform、AlignmentとWrapの組合せを確認する。
4. **実行・実描画**: RavenUITestの実行結果を記録し、Text DemoでASCII/日本語/Resize/Clip/DPIを確認する。異DPIモニター間移動はPhase 10/12とも共有する検証項目。

## Phase 2完了判定

- [x] Layout / Measurement / UILabel / UIElementの既存コードの接続を確認
- [x] 既存テストと未検証領域を棚卸し
- [ ] Layout / Measurementの数値回帰テストを追加・実行
- [ ] UILabel / UIElementの幅依存統合テストを追加・実行
- [ ] Text GlyphのClip / Alignment / Wrapの回帰テストを追加・実行
- [ ] Text DemoでResize・改行・配置・Clipを実描画確認
- [ ] テスト結果と残課題をロードマップに記録しPhase 2完了を判断

Kerning、複雑な文字組み、Grapheme Clusterは現行Phase 2の基本完了条件に混ぜず、後続の拡張課題として扱う。
