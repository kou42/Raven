# Raven プロジェクト整理 M3 Runtime・Editor・起動入口

ソースパスを維持したまま、ソリューションに `Raven.Runtime` と `Raven.Editor` を追加した。ソリューションはRootと6つの静的ライブラリで構成する。

| 所有元 | 主な責務 | コンパイル対象 |
|---|---|---:|
| Runtime | Application・Window・Input、Scene・Physics・Animation・Character・Gltf、描画バックエンドとGLAD、UI、Audio、既存デモ実装 | 294 |
| Editor | EditorLayer、編集コマンド、パネル、ViewModel | 31 |
| Foundation・Data・Network・Diagnostics | M1・M2の所有範囲 | 52 |
| Root | 起動・組み立て、テストとベンチマーク、割り当て計測の実装 | 通常79、CPU UI 80、GPU 82 |

所有一覧は各 `.Items.props` に明記し、Rootからソースとヘッダーのワイルドカードを外した。Rootが全ライブラリと外部ライブラリをリンクする。RuntimeはFoundation・Data・Diagnosticsを参照し、EditorはRuntimeとFoundation・Data・Diagnosticsを参照する。ライブラリ間のプロジェクト参照でビルド順を決め、各アーカイブには自身のオブジェクトを格納する。

`main.cpp` がApplication、シーン、デモとEditorの組み立てを行い、各ライブラリの公開入口を呼ぶ。Editorの製品コードを参照する診断テストはRootのテスト一覧に含める。RuntimeのUIは既存の割り当て計測APIを使い、実行ファイルが `AllocationMeasurement.cpp` を直接リンクする。グローバル `new/delete` を置換する構成でも実行ファイルごとに実装が1つになる。

Debug Physicsの選択最適化はRuntimeへ移した。既定で13本に適用し、`RavenOptimizePhysicsInDebug=false` で解除できる。シェーダー生成はRootにあり、DX12とVulkanの既存条件を使う。Diagnosticsのメタデータ検証はM2と同じくDiagnosticsにある。

## 起動入口の分離範囲

| 種別 | 既存の入口 | M3での所属 |
|---|---|---|
| 通常起動と編集デモ | 引数なし、`--editor-backend-comparison` | RootがRuntimeとEditorを組み立てる |
| アプリ内検証 | `--test-editor-*`、`--test-scene-runtime` など | 呼び出しは `main.cpp`、テストはRoot |
| ベンチマーク | `--benchmark-*`、`--measure-*` | 既存CLIとテスト補助をRootに維持 |
| 描画・Physicsデモ | `--scene-*`、`--clear-*`、`--gpu-sdf-fluid-demo` など | 選択はRoot、デモ実装はRuntime |
| CPU UI・GPUテスト | `RavenUITests.exe`、`RavenUIGPUTests.exe` | 従来どおりRootの起動入口を切り替える |

M3の独立実行入口は既存のCPU UI・GPUテストとする。アプリ内検証・ベンチマーク・デモの分離では、将来いずれも `main.cpp` の分岐とテスト・デモの依存を一緒に移す。

## 構成評価

x64 Debug／Releaseの通常・CPU UI・GPUおよび各計測付き構成と、Win32 Debug／Releaseの通常構成をMSBuildで評価した。14構成すべてでM0のコンパイル対象の集合と一致し、重複はなかった。ライブラリの参照グラフに循環がなく、Runtimeの所有ファイルからEditorの所有ファイルへのincludeもなかった。対象名、計測付き出力先と計測マクロ、Debug Physics最適化の切り替えを照合した。

x64 DebugではRuntimeの全オブジェクトを取り込むリンク検証も実施した。Editorのアーカイブを指定せず、基盤ライブラリ、Windows・描画ライブラリ、実行ファイル側の割り当て計測実装を指定した構成でリンクに成功した。

## 実行検証

| x64の検証 | Debug | Release |
|---|---|---|
| 通常アプリとEditor比較デモ | ビルド・ウィンドウ表示・正常終了 | ビルド・ウィンドウ表示・正常終了 |
| DataDocument編集、Editorのシーン寿命、Scene Runtime | 全件成功 | 全件成功 |
| Editorコマンド・Viewport・Memory/Containersの自己テスト | 全件成功 | Debug専用 |
| CPU UIテスト | ビルド・実行成功 | ビルド・実行成功 |
| GPUテスト | 既定テストとOpenGL・Vulkan・DX12 Compute成功 | 既定テストとOpenGL・Vulkan・DX12 Compute成功 |
| 割り当て計測付きアプリ・CPU UI | ビルド・既存テスト成功 | ビルド・既存テスト成功 |
| 計測付きアプリの変更なし再ビルド | 終了0、実行ファイルと両ライブラリの更新なし | 終了0、実行ファイルと両ライブラリの更新なし |

Editorの編集操作は既存の自動テストで検証した。Editor比較デモは実ウィンドウでOpenGL初期化、比較用の5エンティティと4メッシュ、正常終了を確認した。GPUは実ウィンドウの権限を切り替えて実行した。

割り当て計測付きCPU UIの `--measure-ui-widgets` では、プールなしの100ボタン生成について `allocation_available=1` と、Debug 300回／Release 100回の割り当てを確認した。どちらもM0・M1と一致した。

ビルドにはM0と同じMSPDB回避設定を使った。Win32と割り当て計測付きGPUの組み合わせは構成の静的評価まで実施した。ログは `artifacts/logs/project-org-m3-*` にある。
