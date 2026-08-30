#pragma once

#include "Raven/Math/MathVector.h"

#include <cstddef>
#include <vector>

namespace Raven
{

// ============================================================================
// UIDrawCommandType
// ============================================================================
// UIContextが生成する描画要求の種類です。
//
// 現段階では最初の描画単位としてSolidRectだけを扱います。
// Text / Image / Border等はUIElement側へGPU実装を漏らさず、今後このコマンド列へ
// 追加していく方針です。
enum class UIDrawCommandType
{
    SolidRect
};

// ============================================================================
// UIRect
// ============================================================================
// UI座標系上の矩形です。
// Minを左上、Maxを右下として扱い、座標単位はpixelを基本とします。
struct UIRect
{
    math::Vec2 Min{};
    math::Vec2 Max{};
};

// ============================================================================
// UIDrawCommand
// ============================================================================
// UI Tree / WidgetからRenderer backendへ渡す、GPU API非依存の描画要求です。
//
// 重要:
// この構造体へOpenGLのTexture IDやVertexArray等を直接持たせないことで、
// Editor UIとGame UIのどちらから利用してもPlatform Rendererへ依存しない境界を保ちます。
// 将来Image/Textを追加する際も、Engine側のTexture/Font Handleを利用し、OpenGL固有値は
// UIRenderer実装でのみ解決する設計とします。
struct UIDrawCommand
{
    UIDrawCommandType Type = UIDrawCommandType::SolidRect;
    UIRect Rect{};
    math::Vec4 Color{ 1.0f, 1.0f, 1.0f, 1.0f };
};

// ============================================================================
// UIDrawList
// ============================================================================
// 1 UI frame分の描画要求をCPU側へ蓄積します。
//
// ImGuiと同様に最終的な描画データはframeごとに再構築しますが、UIElementそのものは
// Retained Modeとして別途保持できるよう、DrawListはUI Treeの所有権を一切持ちません。
// これにより、将来のLayout / HitTest / Event処理とRenderingを分離できます。
class UIDrawList
{
public:
    void Clear();

    void AddRect(
        const math::Vec2& min,
        const math::Vec2& max,
        const math::Vec4& color);

    const std::vector<UIDrawCommand>& GetCommands() const;
    std::size_t GetCommandCount() const;
    bool IsEmpty() const;

private:
    std::vector<UIDrawCommand> m_Commands;
};

} // namespace Raven
