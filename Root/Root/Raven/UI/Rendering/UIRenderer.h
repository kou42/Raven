#pragma once

#include "Raven/Math/MathVector.h"

namespace Raven
{

class UIDrawList;

// ============================================================================
// UIRenderer
// ============================================================================
// Raven UIのCPU側描画コマンドを、実際のGraphics APIへ変換するための境界です。
//
// 重要:
// UIContext / Widget側はこの抽象インターフェースだけを参照し、OpenGL等の具体APIを
// 知りません。これによりEditor UIとGame UIで同じUI Tree / DrawListを共有しつつ、
// Renderer backendだけを将来OpenGL / DirectX / Vulkanへ差し替えられます。
//
// 最初の段階ではインターフェースだけを確立します。
// 次段階でOpenGLUIRendererを追加し、動的Vertex/Index Buffer・Orthographic Projection・
// Scissor Clipをここより下の層へ実装します。
class UIRenderer
{
public:
    virtual ~UIRenderer() = default;

    virtual void Render(
        const UIDrawList& drawList,
        const math::Vec2& viewportSize) = 0;
};

} // namespace Raven
