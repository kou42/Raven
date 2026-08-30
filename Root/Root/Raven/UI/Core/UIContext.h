#pragma once

#include "Raven/Core/Base.h"
#include "Raven/Math/MathVector.h"
#include "Raven/UI/Core/UIDrawList.h"
#include "Raven/UI/Core/UIElement.h"
#include "Raven/UI/Rendering/UIRenderer.h"

#include <utility>

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
// UIElement / Layout / Event SystemはこのContextへUIDrawCommandを生成します。
//
// 現在は最初のRetained Mode基盤としてRoot UIElementも所有します。
// Root以下のElement Treeはframeを跨いで保持し、EndFrame()直前にLayoutを解決してUIDrawListへ展開します。
// これによりWidgetのLifetimeとGPUへ渡す一時DrawCommandのLifetimeを分離します。
class UIContext
{
public:
    UIContext()
        : m_RootElement(CreateScope<UIElement>())
    {
    }

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

        // ====================================================================
        // Retained UI Tree -> Layout -> UIDrawList
        // ====================================================================
        // UIElement Treeはframeを跨いで保持し、描画直前にAbsolute / Vertical / Horizontal Layoutを解決して
        // 今frame用DrawCommandへ展開します。将来Measure / Arrangeを分離してもUIContextのframe境界は維持します。
        if (m_RootElement != nullptr)
        {
            m_RootElement->BuildDrawList(m_DrawList);
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

    UIElement& GetRootElement()
    {
        return *m_RootElement;
    }

    const UIElement& GetRootElement() const
    {
        return *m_RootElement;
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
    Scope<UIElement> m_RootElement;
    Scope<UIRenderer> m_Renderer;
    bool m_FrameActive = false;
};

} // namespace Raven
