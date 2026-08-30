#pragma once

#include "Raven/Core/Base.h"
#include "Raven/Math/MathVector.h"
#include "Raven/UI/Core/UIDrawList.h"

#include <algorithm>
#include <utility>
#include <vector>

namespace Raven
{

// ============================================================================
// UILayoutMode / UIThickness
// ============================================================================
// Absoluteは各Childが保持するPositionをそのまま利用します。
// Vertical / HorizontalはParentがChildのPositionを毎frame計算し、Retained Tree上のSizeを使って並べます。
enum class UILayoutMode
{
    Absolute = 0,
    Vertical,
    Horizontal
};

struct UIThickness
{
    float Left = 0.0f;
    float Top = 0.0f;
    float Right = 0.0f;
    float Bottom = 0.0f;

    UIThickness() = default;

    explicit UIThickness(float uniform)
        : Left(uniform), Top(uniform), Right(uniform), Bottom(uniform)
    {
    }

    UIThickness(float horizontal, float vertical)
        : Left(horizontal), Top(vertical), Right(horizontal), Bottom(vertical)
    {
    }
};

// ============================================================================
// UIElement
// ============================================================================
// Raven UIのRetained Mode Treeを構成する最小基底要素です。
//
// UIElement自身は背景やTextを描画しません。Position / Size / Visibility / Parent-Child関係を保持し、
// BuildDrawList()で自分自身と子Elementを順番にUIDrawListへ展開します。
//
// Positionは親Element左上を原点とするローカル座標です。
// Absoluteでは明示Positionを使い、Vertical / HorizontalではParentのLayout結果を同じPositionへ反映します。
// このためWidget描画側はLayout方式を意識せず、最終的な配置結果だけを利用できます。
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

    void SetLayoutMode(UILayoutMode layoutMode)
    {
        m_LayoutMode = layoutMode;
    }

    void SetPadding(const UIThickness& padding)
    {
        m_Padding = padding;
    }

    void SetPadding(float uniformPadding)
    {
        m_Padding = UIThickness(uniformPadding);
    }

    void SetSpacing(float spacing)
    {
        m_Spacing = std::max(0.0f, spacing);
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

    UILayoutMode GetLayoutMode() const
    {
        return m_LayoutMode;
    }

    const UIThickness& GetPadding() const
    {
        return m_Padding;
    }

    float GetSpacing() const
    {
        return m_Spacing;
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

    void BuildDrawList(UIDrawList& drawList)
    {
        // LayoutはDrawList生成より前にTree全体へ適用します。
        // 現段階ではSizeをDesired Sizeとして扱う最小実装ですが、Measure/Arrangeを追加しても
        // Widget側のOnBuildDrawList()を変更せず拡張できる境界にしています。
        ApplyLayoutRecursive();
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
    void ApplyLayoutRecursive()
    {
        if (m_LayoutMode == UILayoutMode::Vertical)
        {
            float cursorY = m_Padding.Top;

            for (auto& child : m_Children)
            {
                if (child == nullptr || child->m_Visible == false)
                {
                    continue;
                }

                child->m_Position = math::Vec2(m_Padding.Left, cursorY);
                cursorY += child->m_Size.y + m_Spacing;
            }
        }
        else if (m_LayoutMode == UILayoutMode::Horizontal)
        {
            float cursorX = m_Padding.Left;

            for (auto& child : m_Children)
            {
                if (child == nullptr || child->m_Visible == false)
                {
                    continue;
                }

                child->m_Position = math::Vec2(cursorX, m_Padding.Top);
                cursorX += child->m_Size.x + m_Spacing;
            }
        }

        // Absoluteの場合はChildが明示したPositionを変更しません。
        // Nested Layoutを可能にするため、親の配置確定後に各ChildのLayoutを再帰的に解決します。
        for (auto& child : m_Children)
        {
            if (child != nullptr && child->m_Visible == true)
            {
                child->ApplyLayoutRecursive();
            }
        }
    }

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
    UIThickness m_Padding{};
    UILayoutMode m_LayoutMode = UILayoutMode::Absolute;
    float m_Spacing = 0.0f;
    bool m_Visible = true;
};

} // namespace Raven
