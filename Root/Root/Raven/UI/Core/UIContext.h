#pragma once

#include "Raven/Core/Base.h"
#include "Raven/Math/MathVector.h"
#include "Raven/UI/Core/UIDrawList.h"
#include "Raven/UI/Rendering/UIRenderer.h"

namespace Raven
{

// ============================================================================
// UIContext
// ============================================================================
// 1つのUI描画対象に対するframe状態を管理します。
//
// 現段階ではApplicationのMain Window用Contextとして利用しますが、Context自体を
// WindowやEditorへ直接依存させていません。このため将来は、
//   - Main Window上のEditor UI
//   - Game View / RenderTexture上のGame UI
//   - World Space UI用の別Context
// のように複数Contextへ拡張できます。
//
// UIContextはRetained UI Treeそのものではなく、「今frameの描画要求」を集約する境界です。
// UIElement / Layout / Event Systemは後続段階でこのContextへUIDrawCommandを生成します。
class UIContext
{
public:
    void BeginFrame(const math::Vec2& viewportSize)
    {
        // 前frameのDrawCommandを必ず破棄してから新しいframeを開始します。
        // UI TreeはRetained Modeとして保持しますが、DrawListはViewport/Layout結果から
        // 毎frame再構築することでResizeやStyle変更を即座に反映できるようにします。
        m_DrawList.Clear();
        m_ViewportSize = viewportSize;
        m_FrameActive = true;
    }

    void EndFrame()
    {
        if (m_FrameActive == false)
        {
            return;
        }

        // Renderer backendがまだ設定されていない期間でもUI構築側を先行実装できるよう、
        // nullptrは正常な状態として扱います。OpenGLUIRenderer追加後はApplication初期化時に
        // SetRenderer()して、この同じframe境界から実描画へ接続します。
        if (m_Renderer != nullptr)
        {
            m_Renderer->Render(m_DrawList, m_ViewportSize);
        }

        m_FrameActive = false;
    }

    void SetRenderer(Scope<UIRenderer> renderer)
    {
        m_Renderer = std::move(renderer);
    }

    UIDrawList& GetDrawList()
    {
        return m_DrawList;
    }

    const UIDrawList& GetDrawList() const
    {
        return m_DrawList;
    }

    const math::Vec2& GetViewportSize() const
    {
        return m_ViewportSize;
    }

    bool IsFrameActive() const
    {
        return m_FrameActive;
    }

private:
    math::Vec2 m_ViewportSize{};
    UIDrawList m_DrawList;
    Scope<UIRenderer> m_Renderer;
    bool m_FrameActive = false;
};

} // namespace Raven
