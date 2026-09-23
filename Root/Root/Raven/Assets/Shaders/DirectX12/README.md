# DX12 Scene Mesh Shader

`SceneMesh.hlsl` は `DX12SceneGraphicsPipeline` の固定Binding契約に合わせた
Entity Mesh描画用のVertex / Pixel Shaderです。

Visual Studioの通常x64ビルド時に、MSBuildの `CompileDX12SceneShaders` が
HLSLから2つのDXILを自動生成します。HLSLの更新時・DXILが存在しない場合のみ
再生成します。DXC（DirectX Shader Compiler）の `dxc.exe` をPATHへ追加してください
（Windows SDKの `WindowsSdkVerBinPath/x64/dxc.exe` も探索します）。
DXCがない場合はビルドをエラー終了させます。

手動で再生成したい場合は、リポジトリの `Root/Root` を作業ディレクトリとして
以下を実行してください。

```powershell
powershell -ExecutionPolicy Bypass -File Raven/Assets/Shaders/DirectX12/BuildSceneMesh.ps1
```

生成されるファイル:

- `Raven/Assets/Shaders/DirectX12/SceneMesh.vs.dxil`（`VSMain`, `vs_6_0`）
- `Raven/Assets/Shaders/DirectX12/SceneMesh.ps.dxil`（`PSMain`, `ps_6_0`）

生成後、Visual StudioでRavenをビルドし、実行時の作業ディレクトリを
`Root/Root` にして `--scene-dx12` を渡します。

```powershell
.\x64\Debug\Root.exe --scene-dx12
```

実行ファイルの場所はVisual StudioのOutput Directory設定によって異なります。
Shader Asset Loaderは実行時の作業ディレクトリから相対パスを解決します。

Demoは3つのEntityが共有するCube Meshを回転描画し、未指定Textureには
1x1の白Textureを使用します。TextureのSRVとTint、Opaque/Transparent用
Pipeline、D32 Depth Bufferを通る最小統合確認です。

確認項目: Shader Assetの読込、Pipeline作成、3 Cube表示、Window Resize、
DX12 Debug Layerの警告、終了時のGPU Resource Lifetime。
実機で確認するまでは、動作確認済みとは扱わないでください。
