# Vulkan Scene Triangle 検証

既存OpenGL Application / Editorとは独立したVulkan Scene描画テストです。通常起動の挙動は変更しません。

## 準備・起動

Vulkan SDKをインストールし、`VULKAN_SDK` 環境変数が設定されていることを確認してください。Visual Studioで `Root/Root/Root.vcxproj` をビルドすると、MSBuildが `$(VULKAN_SDK)\\Bin\\glslc.exe` を呼び出し、2つのGLSLから `.spv` を自動生成します。未生成時とGLSL更新時に生成されるため、手動で `glslc` を実行する必要はありません。

作業ディレクトリを `Root/Root`（Visual Studioでは `$(ProjectDir)`）に設定し、コマンドライン引数 `--scene-triangle-vulkan` で起動してください。プロジェクトは `Raven\\**\\*.cpp` を含むため、新規cppを個別登録する必要はありません。

## 期待する結果

黒い背景にRGB各頂点の色が補間された三角形を描画します。ウィンドウのResize後もPipelineを再生成して描画を続けます。最小化中は描画を一時停止します。

## 制限・確認項目

- Shaderはビルド時にSPIR-Vへ自動コンパイルします。実行時GLSLコンパイルは行いません。SDK未導入または `VULKAN_SDK` 未設定時はビルドエラーで通知します。
- Color FormatはRGBA8_UNORM/BGRA8_UNORMのみ。Surfaceが他のFormatを選んだ場合は初期化失敗を返します。
- Validation Layerを有効にして、初期化、描画、Resize、最小化復帰、終了時の警告を確認してください。
- GPU完了前にBuffer/Pipelineを破棄しないこと。DemoのShutdownはWaitIdle後にBufferを破棄します。
- 本変更では実機ビルド・GPU実行を行っていません。
