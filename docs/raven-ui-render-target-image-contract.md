# Raven UI RenderTarget Image契約

## 責務境界

`UITextureView`はGPU API固有handleを持たず、次のEngine Resource参照だけを保持する。

- Source Asset: `TextureAsset`
- OpenGL等のLegacy RenderTarget: `Texture`
- DirectX 12 / Vulkan等のExplicit RenderTarget: `RHITexture`

`UIImage`と`UIDrawList`は`UITextureView`を保持し、OpenGL UI RendererはLegacy Texture、Explicit UI RendererはRHI Textureを解決する。対応しないBackendのResourceをnative castして利用しない。

## 座標系

- UI配置とMouse入力: Window/UIContextの論理座標。左上原点。
- Raven UI UV: 左上 `(0, 0)`、右下 `(1, 1)`。
- Texture Pixel: `UITextureView::UITextureOrigin`でResource原点を明示する。
- OpenGL Framebuffer Attachment: `BottomLeft`を指定する。
- Source Assetおよび上端がV=0のExplicit Texture: `TopLeft`を指定する。

`UIImage::TryMapScreenPositionToTexturePixel()`は親PositionとVisual Transformを逆変換し、Aspect Fit後の実映像矩形からTexture Pixelへ変換する。UIContextがDPIをWindow論理座標へ集約しているため、Framebuffer倍率をこの変換へ重ねて適用しない。

## AspectとClip

- `Stretch`: Widget矩形全体へ表示する。
- `AspectFit`: Textureの縦横比を維持して中央配置する。
- `UIImage`は自身のBoundsでClipする。
- Aspect Fitで生じるLetterbox領域はPixel変換を失敗させ、PickingやCamera操作を映像外へ伝播させない。

## Lifetime

`UITextureView`とDraw Commandは`Ref`を保持する。Framebuffer ResizeでAttachmentが交換された場合、Widgetへ新しいViewを設定するまで旧Resourceは描画Frame中生存する。WidgetはFramebuffer自体を所有せず、Editor側が毎FrameまたはResize後に現在のAttachmentを再接続する。

## 検証

- Debug x64の通常構成とCPU UI Test構成がBuildできることを確認する。
- CPU TestではAspect Fit、Letterbox除外、BottomLeft UV反転、論理座標からTexture Pixelへの変換、Draw CommandのLifetime保持を検証する。
- 実際のScene/Game Viewへ接続した後は、DPI 100%以外、Framebuffer Resize直後、OpenGLとExplicit Backendの両方で表示とPickingの一致を確認する。
