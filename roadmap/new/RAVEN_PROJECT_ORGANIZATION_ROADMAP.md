# Raven プロジェクト整理ロードマップ

作成日：2026年11月21日

対象：`D:\Engine\Root\Root\Raven`、`Root.vcxproj`、`D:\Engine\Root\Root.sln`

## 1. 目的と前提

Ravenを、責務と依存方向が明確なモジュールへ段階的に整理する。フォルダーごとに機械的にプロジェクトを作るのではなく、独立してビルド・利用・検証できる単位を境界とする。

この文書は、前回の読み取り専用解析を整理した計画書である。作成にあたってRavenのソースやプロジェクト設定は変更していない。解析後の変更は再調査していないため、実装開始前に最新状態との差を確認する。

ここでいうモジュールは責務とビルドの単位であり、C++言語機能のModulesを導入する計画ではない。初期の分割先は静的ライブラリ（`.lib`）を基本とする。

### 基本方針

- 最初はファイルを移動せず、既存パスのまま各プロジェクトの所有ファイルを決める。
- 一つの `.cpp` をコンパイルするプロジェクトは一つに限定する。
- プロジェクト参照は循環させない。密結合部分は当初一つのRuntimeにまとめる。
- テスト・ベンチマーク・デモの依存を、製品機能の依存から区別する。
- プロジェクト分割とツールセット更新を同時に行わない。
- 各マイルストーンで検証し、結果を確認してから次段階へ進む。

## 2. 前回解析で確認した現在地

解析対象は `.cpp` 440件、`.h` 521件、`.inl` 23件の計984ファイル。17機能の外に `sample.cpp` と `sample.h` が各1件含まれる。

機能をまたぐ直接のinclude記述は1,039件。Math・Debug以外の15機能は、機能単位のincludeグラフで一つの強連結成分になっている。Tests・Benchmarksを解析元から除いても、Editorを除く14機能の循環が残る。

これは文字列形式のincludeを集計した静的解析であり、条件付きコンパイルの有効性やリンク時のシンボル依存を確定したものではない。個々のヘッダーが再帰includeしているという意味でもない。

### 特に整理が必要な境界

| 境界 | 現状 | 方針 |
|---|---|---|
| Coreと上位機能 | ApplicationがScene・Renderer・UI・Audioなどを利用する | Coreを基盤とApplication統合に分ける |
| PhysicsとScene | PhysicsがScene・Components・Entityを利用し、SceneもPhysicsを利用する | 初期は同じRuntimeに置く。物理計算とScene連携を後で分ける |
| PhysicsとRenderer | 衝突側がMeshGeometryを利用し、描画変形側がSoftBodyを利用する | CPU形状データと変形連携を整理する |
| AnimationとCharacter・Gltf | ロコモーションやスキニングの実行時連携が混在する | アニメーション計算の核と連携処理を区別する |
| RendererとPlatform | 描画側のfactoryが具体的バックエンドを生成する | 初期は同じGraphicsに置き、必要になれば生成処理を上位へ出す |
| AssetsとRenderer | TextureAsset・ShaderAssetがGPU型と結び付いている | 現在のGPU用Assetsを独立した下位基盤とみなさない |
| DiagnosticsとEditor | Diagnostics内のテストがEditorのViewModelを利用する | ビュー連携テストを上位のテスト側へ分ける |

## 3. 最初の到達構成

まず次の7プロジェクト程度を目指す。Graphics・Physics・Animationの独立は、その後の境界整理を経て判断する。

| プロジェクト | 種別 | 主な責務 |
|---|---|---|
| Root | 実行ファイル | main、起動設定、各機能の組み立て |
| Raven.Foundation | 静的ライブラリ | Math、Coreの汎用基盤 |
| Raven.Data | 静的ライブラリ | Dataの解析・保存・シリアライズ |
| Raven.Network | 静的ライブラリ | 通信・制御メッセージ・Windowsソケット実装 |
| Raven.Diagnostics | 静的ライブラリ | 診断収集・保存・計測・リモート診断 |
| Raven.Runtime | 静的ライブラリ | 当面の描画・Scene・Physics・Animation・Character・Gltf・UI・Audioと統合処理 |
| Raven.Editor | 静的ライブラリ | 編集機能・パネル・編集コマンド |

想定する参照方向は次のとおり。実装シンボルの利用を確認して確定する。

```text
Root → Editor / Runtime
Editor → Runtime / Diagnostics / Data / Foundation
Runtime → Diagnostics / Data / Foundation
Diagnostics → Network / Foundation
Network → Data
Data → Foundation
```

Rootは、mainから直接利用するライブラリも必要に応じて参照する。公開ヘッダーのinclude関係と、リンクすべきライブラリの関係は同一ではない。

トップレベルのDebugはWindows依存の開発支援機能として、当初はRuntimeまたは起動・開発支援側に残す。Foundationには入れない。空で参照のない `sample.cpp` と `sample.h` はM0で削除した。

## 4. マイルストーン

### M0：最新構成と検証基準を確定する

**目的：** 分割前後を比較できる状態を作る。

**対象：** `structure.txt`、`Root.sln`、`Root.vcxproj`、関連props・スクリプト、mainとテスト入口。

実施内容：

- 最新の構成と前回解析との差を確認する。
- 構成別にコンパイル対象を評価し、製品コード・テスト・ベンチマーク・デモを分類する。
- 通常起動、UIテスト、GPUテスト、割り当て計測の設定差を記録する。
- Debug/Release、x64/Win32の対応範囲と、現状の成功・失敗を記録する。
- 検証に必要なSDK・ライブラリ・実行時アセットと起動方法を確認する。

**検証：** 対応環境で既存ビルドと代表テスト・起動を実行し、結果を保存する。

**完了条件：** 現状の基準結果、所有ファイル一覧、構成差、既知の失敗が明確になっている。

**主なリスク：** 分割と無関係な既存不具合、SDK不足、条件付きコンパイルによる構成差。

### M1：Foundationを切り出す

**目的：** 上位機能へ依存しない共通基盤を確立する。

**対象：** Math、CoreのBase・Containers・Memory・Jobs・CPUProfiler・汎用JSONなど、Rootの登録項目、共通props、filters、ソリューション。

実施内容：

- 各ファイルの依存を確認してFoundationの所有一覧を確定する。
- Application、描画依存のWindow・WindowManager、GLFW依存のInput、統合テストは切り出し対象から外す。
- 共通コンパイラ設定をまとめ、Foundationを静的ライブラリ化する。
- Rootのワイルドカードから移管済みcppを除外し、Foundationへの参照を追加する。

**検証：** Foundation単独ビルド、既存のMath・Containers・Memory・Jobs関連検証、Rootのビルドと代表起動。

**完了条件：** FoundationからScene・Renderer・UI・Applicationへの依存がなく、cppの二重所属がない。

**主なリスク：** ヘッダー経由の隠れた依存、テンプレート定義の不足、同名obj、計測用の定義・グローバルnew/deleteの所属。

### M2：Data・Network・Diagnosticsを順に切り出す

**目的：** 基盤上に一方向の機能層を作る。

**対象：** Data、Network、Diagnostics、各テスト、診断生成検証ターゲット、プロジェクト設定。

実施内容：

1. DataをFoundation利用側として独立させる。
2. NetworkをData利用側として独立させる。
3. DiagnosticsをFoundation・Network利用側として独立させる。
4. EditorのViewModelやRendererを使う診断テストを、上位のテスト実行ファイル側へ分類する。
5. `VerifyDiagnosticMessages` の実行責任と、生成済みヘッダーを利用する側の順序を決める。

リモート診断を含める限り、DiagnosticsはNetworkへ依存する。診断の核だけを小さく保つ必要が生じた場合に、RemoteDiagnosticsの追加分離を検討する。

**検証：** データ形式の読み書き・シリアライズ、通信コーデック、診断収集・保存・リモート接続、診断メタデータ検証。

**完了条件：** Data → Foundation、Network → Data、Diagnostics → Network/Foundationの方向が成立し、製品DiagnosticsからEditor・Rendererへの参照がない。

**主なリスク：** テスト補助コードへの依存、Winsockのリンク漏れ、診断検証の実行漏れ。

### M3：Runtime・Editor・起動入口を分ける

**目的：** 残る密結合部分を保ちながら、上位の構成を整理する。

**対象：** 残るRaven実装、CoreのApplication、Editor、main、テスト・デモ入口、Rootのワイルドカード。

実施内容：

- Scene・Physics・Animation・Character・Gltf・描画・UIなどを、当初は一つのRuntimeへまとめる。
- Editorの製品実装を独立させ、Runtime側からEditorへ依存させない。
- Rootは起動・組み立てを担当する。
- mainに混在する自己テスト・ベンチマーク・デモ選択を確認し、別実行ファイルへの分離範囲を決める。
- UIテストとGPUテストの入口・出力先を維持する。

**検証：** 通常起動、Editorの編集操作、既存の自己テスト呼び出し、UI・GPUテスト、計測構成。

**完了条件：** 最初の7プロジェクト構成が循環参照なしで成立し、既存の検証入口が失われていない。

**主なリスク：** 静的ライブラリ化による未参照objの取り込み差、登録処理や副作用の欠落、mainの重複定義。

### M4：GraphicsとRuntimeの境界を整理する

**目的：** 描画機能をScene・Physicsの実行時統合から独立させる。

**対象：** Renderer、Renderer/RHI、Platform、GPU用Assets、Mesh変形、Debug描画、シェーダー生成。

実施内容：

- Rendererとバックエンドは当初一つのGraphicsにまとめる。
- Sceneの描画情報構築、Physics連携、Animation/PhysicsのDebug描画呼び出しをRuntime側へ整理する。
- MeshGeometryなどCPU側データとGPU資源の契約を確認する。
- GPU用AssetsはGraphicsに含め、Assets全体を下位基盤として切り出さない。
- シェーダー生成を必要に応じて一つのUtilityプロジェクトへ集約する。

**検証：** 対応するOpenGL・DX12・Vulkanの起動、描画、UI、ピッキング、変形、シェーダー再生成。

**完了条件：** Runtime → Graphicsの方向が成立し、GraphicsからRuntimeへの参照がない。

**主なリスク：** フレーム処理順、GPU資源の寿命、CPU/GPU同期、実行時のアセットパス。

### M5：必要な機能だけ追加で独立させる

**目的：** 独立利用やテストに価値がある部分を、実際の需要に応じて分ける。

| 候補 | 独立前に整理する内容 |
|---|---|
| Animationの核 | Pose・Clip・Blendなどと、Scene・Character・Gltf・描画連携を分ける |
| Physicsの核 | Scene所有権・Components・Entityへの依存と、純粋な計算処理を分ける |
| GltfImporter | ファイル解析・変換と、Scene生成・スキニング実行時処理を分ける |
| UI | 汎用UIと、Animation・Assets・描画への連携を確認する |
| Audio | Foundationだけを利用するサービス境界を維持する |
| RHIと各バックエンド | 具体的実装を生成するfactoryを上位へ置く |

**検証：** 各候補の単独テスト・最小利用例と、Runtimeを通した統合動作。

**完了条件：** 独立による利点が確認でき、循環参照や不必要な公開APIを増やしていない。

**主なリスク：** 分割過多による設定負担、過剰な抽象化、更新順や寿命の暗黙的な前提。

## 5. ビルド設定の移行チェック

### コンパイル対象と生成物

- 現在の `Raven\**\*.cpp` はTests・Benchmarks・デモも含む。移管時に所有元から必ず外す。
- 移行中のワイルドカードは除外を明示し、最終的には所有ファイル一覧をレビュー可能にする。
- `.inl` は公開ヘッダーから参照する定義として管理し、独立した翻訳単位にしない。
- Core/DataのJsonParser・JsonWriterにあるobj名の個別指定は、移管後の衝突有無を確認して扱う。
- IntDirはプロジェクト・構成・プラットフォーム・計測種別ごとに分ける。
- `.vcxproj.filters` と、必要な場合は `structure.txt` を実際の構成に合わせる。

### 共通設定と参照

- 現状のv143を維持し、VS2026でのツールセット更新は別に検証する。
- C++23、`/utf-8`、`NOMINMAX`、`_ENABLE_EXTENDED_ALIGNED_STORAGE` を引き継ぐ。
- CRT・Debug/Release・最適化・アラインメントなどの設定をライブラリ間で揃える。
- Physicsの特定ファイルとRendererのMeshに適用されているDebug時の個別最適化を引き継ぐ。
- プロジェクト配置変更で `$(ProjectDir)` の意味が変わるため、共通のRavenルートを定義する。
- `ProjectReference` でビルド順とリンク関係を管理し、単なるソリューションの順序設定で代用しない。
- GLFW・Winsock・Vulkan・DX12関連ライブラリが最終実行ファイルへ届くことを確認する。

### 構成固有の設定

- `RavenUITest` と `RavenUIGPUTest` の入口・優先条件・TargetName・IntDirを維持する。
- `RavenUITest.props` は現在Debug/x64でimportされる。全構成に同じ設定があると仮定しない。
- `RavenUIAllocationProfiling` の定義を必要なライブラリにも適用し、通常構成の生成物と混ぜない。
- リンク時のデバッグ情報生成を切り替える診断用設定を維持する。
- Win32とx64のGPU関連設定差を確認する。

## 6. カスタムビルドターゲットの扱い

| ターゲット | 現状 | 移行時の扱い |
|---|---|---|
| CompileDX12SceneShaders | x64、UI/GPUテスト以外。DXILを4件生成 | 初期はRootに残す |
| CompileDX12UIShaders | 同じ条件。DXILを2件生成 | 初期はRootに残す |
| CompileVulkanSceneShaders | GLSL6件をSPIR-V化。DX12と同じテスト除外条件はない | 現在の条件を保って移行する |
| VerifyDiagnosticMessages | ClCompile前に生成済み情報を `-Check` で検証 | Diagnostics側へ集約する段階で順序を再設計する |

同じ生成ターゲットを複数プロジェクトへコピーしない。並列実行で同一出力を更新する構成を避ける。

将来Utilityプロジェクトへ集約する際は、次も確認する。

- 生成先である `Raven\Assets\Shaders` と実行時の読み込みパス。
- 初回生成、変更後の再生成、変更なしの増分ビルド。
- 生成スクリプトと、必要ならシェーダーのincludeファイルも入力依存に含めること。
- ライブラリ単独ビルドと実行ファイルのビルドで必要な処理が異なること。
- 診断生成情報を利用するcppより先に検証が終わること。

## 7. 各段階で共通の完了確認

1. 各cppの所有プロジェクトが一意である。
2. プロジェクト参照が循環していない。
3. クリーンビルドと通常の増分ビルドが成立する。
4. 対応する代表機能・テスト・起動方法が維持される。
5. テスト・デモ・計測用設定が通常構成へ意図せず混入していない。
6. シェーダー生成と診断検証の実行責任が明確である。
7. 変更ファイル、検証結果、未検証事項、残課題が報告されている。

実行できなかった検証は、その理由と代替の静的確認を記録し、成功扱いにしない。

## 8. 進め方

最初の作業はM0とし、最新状態と検証基準を確定する。その後、M1のFoundation切り出しを一つの実装単位として扱う。

Ravenのリポジトリ指示に従い、実装するマイルストーンの開始前に、目的・対象ファイル・ビルドとテスト・完了条件・既知のリスクを提示して承認を得る。承認は計画全体の一括実行とはみなさず、完了報告後に次段階へ進む範囲を確認する。

M0の結果は [検証基準](RAVEN_PROJECT_ORGANIZATION_M0_BASELINE.md) と [構成別ソース一覧](RAVEN_PROJECT_ORGANIZATION_M0_SOURCES.csv) に記録した。

M1の所有範囲とM0からの検証結果は [Foundationの切り出し](RAVEN_PROJECT_ORGANIZATION_M1_FOUNDATION.md) に記録した。

M2の依存関係と検証結果は [Data・Network・Diagnosticsの切り出し](RAVEN_PROJECT_ORGANIZATION_M2_SERVICES.md) に記録した。

M3の所有範囲・起動入口と検証結果は [Runtime・Editor・起動入口の分離](RAVEN_PROJECT_ORGANIZATION_M3_RUNTIME_EDITOR.md) に記録した。

M4の描画境界と検証結果は [Graphics・Runtimeの分離](RAVEN_PROJECT_ORGANIZATION_M4_GRAPHICS.md) に記録した。

M5はAudioを対象にし、所有範囲と検証結果を [Audioの分離](RAVEN_PROJECT_ORGANIZATION_M5_AUDIO.md) に記録した。
