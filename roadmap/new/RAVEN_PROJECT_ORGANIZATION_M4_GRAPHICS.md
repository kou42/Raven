# Raven プロジェクト整理 M4 Graphics・Runtime

`Raven.Graphics` を静的ライブラリとして追加し、Runtimeから参照する。GraphicsはFoundationとDiagnosticsを直接参照する。Rootは実行ファイルのリンク時にGraphicsと既存ライブラリ、外部ライブラリを解決する。

| 所有元 | 主な責務 | コンパイル対象 |
|---|---|---:|
| Graphics | Renderer、描画キュー、RHI、OpenGL・DX12・Vulkanの低レベル実装、Window・入力のプラットフォーム契約、GLAD、GPU向けShader・Texture Assets | 81 |
| Runtime | Application、Sceneからの描画情報収集、Animation・Physics、メッシュ変形、Runtime UI、DX12・Vulkanのシーン統合とデモ | 213 |

ソースのパスを維持し、`Raven.Graphics.Items.props` と `Raven.Runtime.Items.props` で所有を明示した。Graphicsが持つ `MeshGeometry` はCPU側の頂点・インデックス・境界データ。Graphicsの `Mesh` がGPU資源と同期し、SceneやPhysicsを参照する変形はRuntimeが実行する。

SceneはPhysicsとAnimationのデバッグ描画を同期コールバックとしてRendererへ渡す。従来どおりLegacyではSurface描画の後に重ね、Explicitではフレーム準備前までに同じLine Queueへ登録する。Rendererはコールバックを保持しない。RHIの描画項目ビルダーはview・projection行列とメッシュのスナップショットを受け取り、SceneCameraを扱う呼び出し元はRuntime側にある。

DX12の共有メッシュ準備はシーン統合側から呼ぶ。ネイティブDeviceが利用でき、Command Listの記録を始める前という既存の条件を維持した。Graphics側のDeviceはContextを借用し、GPU BufferとTextureの生成を担当する。作成した資源の所有権やフレームの同期順は既存のまま。

Debug最適化はRuntimeの12本とGraphicsの `Mesh.cpp` 1本に引き継いだ。いずれも `RavenOptimizePhysicsInDebug=false` で解除できる。Rootにあるシェーダーターゲットの条件と実行時の資産パスを維持した。

## 構成とシェーダー

8プロジェクトの参照グラフに循環がなく、Graphicsのローカルincludeは自身と下位プロジェクトの所有ファイルに解決した。14構成でソースの二重所属がなく、コンパイル対象の集合はM0と一致した。

Graphicsの全オブジェクトを強制的に取り込んだVulkanピッキングテストを、RuntimeとEditorのアーカイブを指定せずにリンクした。テストでは既存のRootターゲットを使って隔離ディレクトリに再生成したSPIR-Vを読み込んだ。既存ターゲットでDXIL 6本、SPIR-V 6本を生成でき、変更なしの再実行では全12本を再利用した。

## 実行検証

| x64の検証 | Debug | Release |
|---|---|---|
| 通常アプリ、CPU UI、GPU、計測付きCPU UI | 全構成ビルド成功 | 全構成ビルド成功 |
| OpenGL通常起動、DX12・VulkanのEditor比較デモ | ウィンドウ表示・正常終了 | ウィンドウ表示・正常終了 |
| GPU既定テスト、3バックエンドのComputeとUI描画比較 | 全件成功 | 全件成功 |
| メッシュ変形、SoftBody統合、Explicit Frame | 既存テスト成功 | SoftBody・Explicit FrameのCLIはDebug専用 |
| 再生成したシェーダーからの読み込み | Vulkan実GPUピッキング成功、Validationエラー0 | DX12・VulkanのEditor比較デモが正常終了 |

計測付きCPU UIは、プールなしの100ボタン生成がDebug 300回、Release 100回でM3と一致した。ピッキングはVulkanで実GPUの読み戻しを検証し、DX12は既存のExplicit Frameテストでパイプライン契約を確認した。

単独の `--scene-dx12` と `--scene-vulkan` はDebug・Releaseとも終了コード1。M3の実行ファイルでも同じ終了コードを確認した。Vulkanは両マイルストーンでUIフレーム準備失敗を出力した。両バックエンドのEditor比較デモとGPUテストは通過した。

ビルドはM0と同じMSPDB回避設定を使った。Win32と割り当て計測付きGPUは構成の静的評価まで実施した。ログは `artifacts/logs/project-org-m4-*` にある。
