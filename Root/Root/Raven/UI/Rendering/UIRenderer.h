#pragma once

#include "Raven/Core/Base.h"
#include "Raven/Math/MathVector.h"

namespace Raven
{

class UIDrawList;

// ============================================================================
// UIRenderer
// ============================================================================
// Raven UIのCPU側描画コマンドを、実際のGraphics APIへ変換するための境界です。
//
// UIContext / Widget側はこの抽象インターフェースだけを参照し、OpenGL等の具体APIを
// 知りません。Editor UIとGame UIで同じUI Tree / DrawListを共有しつつ、
// Renderer backendだけを差し替えられる構造にします。
class UIRenderer
{
public:
    virtual ~UIRenderer() = default;

    virtual void Render(
        const UIDrawList& drawList,
        const math::Vec2& viewportSize) = 0;

    // 現在有効なRendererAPIに対応するUIRenderer実装を生成します。
    // Application側へOpenGL具体型を漏らさないため、生成責務をこのFactoryへ集約します。
    static Scope<UIRenderer> Create();
};

} // namespace Raven
