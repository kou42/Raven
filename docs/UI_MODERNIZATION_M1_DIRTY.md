# UI Modernization M1: Dirty Flag拡張

## 目的と責務

UIElementの既存Measure/Arrange Dirtyに、Paint/Geometry/Styleの通知経路を追加する。
UIContextはTheme/DPIのContext単位の変更を通知し、Widgetは自身の表示データ変更を通知する。
M1ではDrawListを毎Frame再構築し、Dirty値から描画を省略しない。GPU資源の所有権と寿命も従来どおり。

## 無効化規則

| 変更 | Measure | Arrange | Paint | Geometry | Style |
|---|---|---|---|---|---|
| Size/Preferred/Min/Max/Padding/Margin/Spacing/Text | あり | あり | あり | あり | なし |
| Position/Alignment/Painter順 | なし | あり | あり | あり | なし |
| Rotation/Scale/Pivot/Clip | なし | なし | あり | あり | なし |
| Color/Tint/Opacity/Hover/Pressed | なし | なし | あり | なし | なし |
| Theme | なし | なし | あり | あり（保守的） | あり |
| DPI/UserScale | DIP寸法/Fontに応じる | 寸法に応じる | あり | あり | あり |
| Context移譲 | DIP寸法/Fontに応じる | 寸法に応じる | あり | あり | あり |
| Focus/入力イベント | Widgetの従来処理に従う | 同左 | あり | あり（保守的） | なし |

- Dirtyは自身と全祖先へ集約し、兄弟へは伝えない。
- Opacity/Tint/Transform/子Clipは継承されるため、子孫へもVisual Dirtyを伝える。ClipSelfは自身と祖先のみ。
- LayoutのDirtyは全祖先へ伝える。以前のArrangeは直親のみだったため、深い階層でRootが再配置を開始できない経路を修正した。
- Layout確定前の子孫の最終座標は未確定。Geometryフラグは最終頂点の差分ではなく、再構築が必要な変更の通知である。
- Paintだけでも頂点Colorは変わる。Geometryがcleanという理由だけでGPU頂点Bufferを再利用してはいけない。
- Geometry Builder/Backendは従来どおり全DrawListを処理する。
- 構築直前に訪問ElementのVisual Dirtyを消費する。構築中に追加された通知は次Frameへ残す。
- 非表示の子孫は走査せずDirtyを保持する。再表示がMeasure/Arrangeを無効化するため、再表示時に再構築される。
- Rotationは従来のラジアン、Pivotは矩形に対する正規化値、Transformは既存の親子Affine積順を維持。

## 同値更新

正規化後の値と指定単位を比較する。OpacityはClamp後、Sizeは制約適用後に比較する。
DIP→Pixel/Pixel→DIPは、現在の実値が等しくても将来のDPI応答が異なるため更新する。
SetPosition/SetSizeはArrange済みの実値への即時再指定も維持する。
Themeは型ごとのdefault比較を使用し、paddingを含むmemcmpや手書きのフィールド一覧には依存しない。
色の初回Overrideは将来のTheme追従を変えるため、格納色が等しくても通知する。
UIInputText::SetTextはIME取消/Selection等を伴う明示操作なので、文字列だけによる早期returnを追加していない。
Font/Texture等の可変Resourceは、同じ参照先でも内部内容が変わるため明示再設定で無効化する。

## Widget対応とCacheへの境界

UIPanel/UIButton/UICircle/UIPolygon/UILabel/UIImage/UISliderの主要な表示Setterへ通知を追加した。
Mouse/Key/Character/IME/Drag/Focusの共通配送境界は保守的にGeometryを通知する。
外部データを描く独自Widgetは公開InvalidatePaint/InvalidateGeometry/InvalidateStyleを利用できる。

Tree/Table/Tab/Window等には、外部Modelや既存のinline Setterから直接表示状態を変える経路も残る。
M1のDirtyは汎用的なCache有効性の証明ではない。M2はWidgetごとの通知監査とCache対象の明示的な選定を必要とする。
未監査Widgetを含むSubtreeをDirtyフラグだけでCacheしてはいけない。
Resourceの内容世代、Popup/Overlay、Clip/Z順/Context境界もM2で別途扱う。

## 検証

TestUIDirtyInvalidationを既存RavenUITestへ追加。
同値Setter 60 Frameで実Measure回数が増えず、値変更時には増えることを確認する。
Paintの祖先伝播と兄弟の独立、Opacity継承、Transform、Clip、深いArrange、Theme、TextのGlyph数、
DIP/Pixel切替、DPI、非表示/再表示をDirtyと実DrawCommandの両方で検証する。
既存の入力/Undo/IME/移譲/再配置テストも同時実行する。

ビルド条件は[M0の環境回避](UI_MODERNIZATION_M0_BASELINE.md)と同じ。
`RavenUITest=true`、Debug/Release、`RavenGenerateLinkDebugInformation=false`を使用する。

2026-11-16実行。最終の検証結果:

| 検証 | 結果 |
|---|---|
| Debug UIテストターゲットビルド | 成功・終了コード0 |
| Release UIテストターゲットビルド | 成功・終了コード0 |
| Debug UI回帰全体（新Dirty回帰 + Baseline 9ケース含む） | 成功・終了コード0 |
| Release UI回帰全体（同上） | 成功・終了コード0 |
| Debug OpenGL GPU統合の再ビルド | 成功・終了コード0 |
| Debug OpenGL GPU統合実行 | 成功・終了コード0。Arial Font Atlas/Texture転送、共有Context描画/Resize、切り離し/復帰3回 |
| diff whitespace確認 | git diff --check成功 |
| 通常Editorの手操作、異DPI、DX12/VulkanのDirty変更後実描画 | 未実施 |

UIElementのヘッダーから実装へ移した関数に対して、途中ビルドでは古いinline定義のObjectが混在しLNK2005/LNK1169が発生した。
ヘッダー依存の対象を再コンパイルした最終ビルドで解消している。
既存のC4244とDebugのLNK4098は継続。今回の追加箇所のWarningは確認されなかった。
ローカルログは`ui-m1-debug-final-build.log` / `ui-m1-release-final-build.log` / `ui-m1-*-tests.log` / `ui-m1-gpu-build.log`。

## 性能比較と制限

[全18ケースの測定CSV](UI_MODERNIZATION_M1_MEASUREMENTS.csv)を保存した。
全ビルド終了後にRelease→Debugを順番に実行し、測定中に別のビルド/テストは走らせていない。
条件はM0のまま。時間は平均、Count/容量は最終Frame、Cpp Count/Bytesは平均。
Allocation Profileは無効なのでCppの0は未測定を意味する。

| 構成 / 10,000 Widget | M0 Build平均ms | M1 Build平均ms | M1 Layout平均ms |
|---|---:|---:|---:|
| Debug / static | 19.5836 | 20.4325 | 0.0001 |
| Debug / update | 25.2458 | 27.2470 | 7.1406 |
| Debug / scroll | 19.3605 | 24.7118 | 4.5092 |
| Release / static | 0.6498 | 0.8091 | 0.0000 |
| Release / update | 0.9642 | 1.0620 | 0.4154 |
| Release / scroll | 0.6162 | 0.9798 | 0.3393 |

同値更新についてはMeasureProbeで60 Frame連続更新して実Measure呼び出しが増えないことを確認した。
値を変更した際には呼び出しが増えるため、Layoutそのものを省いているわけではない。
一方、既存Baselineは毎Frame値を変えるupdateケースなので、この同値削減の利益を直接測るケースではない。

M1の全ケースで性能が向上したとは扱わない。静的ケースにもDirty追跡の処理が追加されている。
Scrollでは位置変更がRootまでArrangeを伝えるようになり、以前省略されていたLayoutが実行される。
この正しさの修正によるLayout負荷と、同値更新の抑制による削減は区別する。
各測定は単一ホストの一回の実行であり、M0との時間差を厳密な統計的差とは断定しない。
Command/Vertex/Index数はM0と全ケースで同じ。最適化利益の本格比較はM2の対象である。

Raven配下のファイル追加/削除/移動はないため、structure.txtとVisual Studio Filterの構成変更はない。
次はM2の開始前に、Cache対象Widgetと未監査更新経路を具体化して別承認を得る。
