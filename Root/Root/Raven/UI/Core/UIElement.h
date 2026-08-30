#pragma once

#include "Raven/Core/Base.h"
#include "Raven/Math/MathVector.h"
#include "Raven/UI/Core/UIDrawList.h"

#include <utility>
#include <vector>

namespace Raven
{

// ============================================================================
// UIElement
// ============================================================================
// Raven UIのRetained Mode Treeを構成する最小基底要素です。
//
// UIElement自身は背景やTextを描画しません。Position / Size / Visibility / Parent-Child関係を保持し、
// BuildDrawList()で自分自身と子Elementを順番にUIDrawListへ展開します。
//
// Positionは親Element左上を原点とするローカル座標です。
// 現段階ではAbsolute配置だけを扱い、Vertical / Horizontal等のLayout Engineは後続実装で
// Arrange結果として同じPosition / Sizeへ反映する方針です。
class UIElement
{
public:
    UIElement() = default;
    virtual ~UIElement() = default;

    UIElement(const UIElement&) = delete;
    UIElement& operator=(const UIElement&) = delete;

    UIElement(UIElement&&) = delete;
    UIElement& operator=(UIElement&&) = delete;

    UIElement* AddChild(Scope<UIElement> child)
    {
        if (child == nullptr)
        {
            return nullptr;
        }

        // ChildのLifetimeはParentへ移譲します。
        // Parentは非所有pointerだけをChildへ渡し、循環所有を作らないようにします。
        child->m_Parent = this;
        UIElement* childPointer = child.get();
        m_Children.push_back(std::move(child));
        return childPointer;
    }

    void ClearChildren()
    {
        for (auto& child : m_Children)
        {
            if (child != nullptr)
            {
                child->m_Parent = nullptr;
            }
        }

        m_Children.clear();
    }

    void SetPosition(const math::Vec2& position)
    {
        m_Position = position;
    }

    void SetSize(const math::Vec2& size)
    {
        m_Size = size;
    }

    void SetVisible(bool visible)
    {
        m_Visible = visible;
    }

    const math::Vec2& GetPosition() const
    {
        return m_Position;
    }

    const math::Vec2& GetSize() const
    {
        return m_Size;
    }

    bool IsVisible() const
    {
        return m_Visible;
    }

    UIElement* GetParent()
    {
        return m_Parent;
    }

    const UIElement* GetParent() const
    {
        return m_Parent;
    }

    const std::vector<Scope<UIElement>>& GetChildren() const
    {
        return m_Children;
    }

    void BuildDrawList(UIDrawList& drawList) const
    {
        BuildDrawListRecursive(drawList, math::Vec2(0.0f, 0.0f));
    }

protected:
    // 派生Widgetは自身の描画だけをここへ追加します。
    // Child走査はUIElement基底が一元管理することで、WidgetごとにTree Traversalを重複実装しません。
    virtual void OnBuildDrawList(
        UIDrawList& drawList,
        const math::Vec2& absolutePosition) const
    {
        static_cast<void>(drawList);
        static_cast<void>(absolutePosition);
    }

private:
    void BuildDrawListRecursive(
        UIDrawList& drawList,
        const math::Vec2& parentAbsolutePosition) const
    {
        if (m_Visible == false)
        {
            return;
        }

        const math::Vec2 absolutePosition(
            parentAbsolutePosition.x + m_Position.x,
            parentAbsolutePosition.y + m_Position.y);

        OnBuildDrawList(drawList, absolutePosition);

        // Parentを先に描画し、Childを後から描画することで、現段階では単純なPainter's Orderを採用します。
        // ZIndex / Clip / Layer分離は後続実装で追加します。
        for (const auto& child : m_Children)
        {
            if (child != nullptr)
            {
                child->BuildDrawListRecursive(drawList, absolutePosition);
            }
        }
    }

private:
    math::Vec2 m_Position{};
    math::Vec2 m_Size{};
    UIElement* m_Parent = nullptr;
    std::vector<Scope<UIElement>> m_Children;
    bool m_Visible = true;
};

} // namespace Raven
