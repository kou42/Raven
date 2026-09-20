# Vulkan Scene Triangle 検証

既存OpenGL Application / Editorとは独立したVulkan Scene描画テストです。通常起動の挙動は変更しません。

## 準備

Vulkan SDKの `glslc` をPATHへ通し、作業ディレクトリを `Root/Root` にして次を実行します。

```powershell
glslc Raven/Assets/Shaders/Vulkan/SceneTriangle.vert -o Raven/Assets/Shaders/Vulkan/SceneTriangle.vert.spv
glslc Raven/Assets/Shaders/Vulkan/SceneTriangle.frag -o Raven/Assets/Shaders/Vulkan/SceneTriangle.frag.spv
```

Visual Studioで `Root/Root/Root.vcxproj` をビルドし、作業ディレクトリを `Root/Root` に設定して、コマンドライン引数 `--scene-triangle-vulkan` で起動してください。プロジェクトは `Raven\\**\\*.cpp` を含むため、新規cppを個別登録する必要はありません。

## 期待する結果

黒い背景にRGB各頂点の色が補間された三角形を描画します。ウィンドウのResize後もPipelineを再生成して描画を続けます。最小化中は描画を一時停止します。

## 制限・確認項目

- Shaderは上記2ファイルをSPIR-Vへ事前コンパイルしてください。実行時GLSLコンパイルは行いません。
- Color FormatはRGBA8_UNORM/BGRA8_UNORMのみ。Surfaceが他のFormatを選んだ場合は初期化失敗を返します。
- Validation Layerを有効にして、初期化、描画、Resize、最小化復帰、終了時の警告を確認してください。
- GPU完了前にBuffer/Pipelineを破棄しないこと。DemoのShutdownはWaitIdle後にBufferを破棄します。
- 本変更では実機ビルド・GPU実行を行っていません。
