#pragma once

#include "Raven/Core/Base.h"
#include "Raven/Math/MathVector.h"
#include "Raven/UI/Core/UIDrawList.h"

#include <algorithm>
#include <limits>
#include <utility>
#include <vector>

namespace Raven
{

// ============================================================================
// UILayoutMode / UIAlignment / UIThickness
// ============================================================================
// Absoluteは各Childが保持するPositionをそのまま利用します。
// Vertical / HorizontalはMeasureで必要Sizeを集計し、ArrangeでParentから与えられた領域へChildを配置します。
enum class UILayoutMode
{
    Absolute = 0,
    Vertical,
    Horizontal
};

enum class UIAlignment
{
    Start = 0,
    Center,
    End,
    Stretch
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
// BuildDrawList()でLayoutを解決してから自分自身と子ElementをUIDrawListへ展開します。
//
// Positionは親Element左上を原点とするローカル座標です。
// Absoluteでは明示Positionを使い、Vertical / HorizontalではArrange結果を同じPosition / Sizeへ反映します。
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

    void SetPosition(const math::Vec2& position) { m_Position = position; }

    void SetSize(const math::Vec2& size)
    {
        m_PreferredSize = ClampSize(size);
        m_Size = m_PreferredSize;
    }

    void SetPreferredSize(const math::Vec2& size) { m_PreferredSize = ClampSize(size); }
    void SetMinSize(const math::Vec2& size) { m_MinSize = math::Vec2(std::max(0.0f, size.x), std::max(0.0f, size.y)); }
    void SetMaxSize(const math::Vec2& size) { m_MaxSize = math::Vec2(std::max(0.0f, size.x), std::max(0.0f, size.y)); }
    void SetVisible(bool visible) { m_Visible = visible; }
    void SetLayoutMode(UILayoutMode layoutMode) { m_LayoutMode = layoutMode; }
    void SetHorizontalAlignment(UIAlignment alignment) { m_HorizontalAlignment = alignment; }
    void SetVerticalAlignment(UIAlignment alignment) { m_VerticalAlignment = alignment; }
    void SetPadding(const UIThickness& padding) { m_Padding = padding; }
    void SetPadding(float uniformPadding) { m_Padding = UIThickness(uniformPadding); }
    void SetSpacing(float spacing) { m_Spacing = std::max(0.0f, spacing); }

    const math::Vec2& GetPosition() const { return m_Position; }
    const math::Vec2& GetSize() const { return m_Size; }
    const math::Vec2& GetPreferredSize() const { return m_PreferredSize; }
    const math::Vec2& GetDesiredSize() const { return m_DesiredSize; }
    bool IsVisible() const { return m_Visible; }
    UILayoutMode GetLayoutMode() const { return m_LayoutMode; }
    UIAlignment GetHorizontalAlignment() const { return m_HorizontalAlignment; }
    UIAlignment GetVerticalAlignment() const { return m_VerticalAlignment; }
    const UIThickness& GetPadding() const { return m_Padding; }
    float GetSpacing() const { return m_Spacing; }
    UIElement* GetParent() { return m_Parent; }
    const UIElement* GetParent() const { return m_Parent; }
    const std::vector<Scope<UIElement>>& GetChildren() const { return m_Children; }

    void BuildDrawList(UIDrawList& drawList)
    {
        // ====================================================================
        // Measure -> Arrange -> DrawList
        // ====================================================================
        // MeasureはLeafからParentへ必要Sizeを集約し、ArrangeはParentからChildへ実際の領域を配ります。
        // 描画とLayoutを分離しておくことで、Text計測やEditor Property Rowなどを後から追加しても
        // OnBuildDrawList()側へLayout条件を持ち込まずに拡張できます。
        MeasureRecursive();
        ArrangeRecursive(m_Position, ResolveRootSize());
        BuildDrawListRecursive(drawList, math::Vec2(0.0f, 0.0f));
    }

protected:
    virtual void OnBuildDrawList(UIDrawList& drawList, const math::Vec2& absolutePosition) const
    {
        static_cast<void>(drawList);
        static_cast<void>(absolutePosition);
    }

private:
    math::Vec2 ClampSize(const math::Vec2& size) const
    {
        return math::Vec2(
            std::clamp(size.x, m_MinSize.x, m_MaxSize.x),
            std::clamp(size.y, m_MinSize.y, m_MaxSize.y));
    }

    math::Vec2 ResolveRootSize() const
    {
        math::Vec2 result = m_PreferredSize;
        if (result.x <= 0.0f) { result.x = m_DesiredSize.x; }
        if (result.y <= 0.0f) { result.y = m_DesiredSize.y; }
        return ClampSize(result);
    }

    void MeasureRecursive()
    {
        if (m_Visible == false)
        {
            m_DesiredSize = math::Vec2(0.0f, 0.0f);
            return;
        }

        for (auto& child : m_Children)
        {
            if (child != nullptr)
            {
                child->MeasureRecursive();
            }
        }

        math::Vec2 contentSize(0.0f, 0.0f);
        uint32_t visibleChildCount = 0u;

        for (const auto& child : m_Children)
        {
            if (child == nullptr || child->m_Visible == false)
            {
                continue;
            }

            ++visibleChildCount;
            if (m_LayoutMode == UILayoutMode::Vertical)
            {
                contentSize.x = std::max(contentSize.x, child->m_DesiredSize.x);
                contentSize.y += child->m_DesiredSize.y;
            }
            else if (m_LayoutMode == UILayoutMode::Horizontal)
            {
                contentSize.x += child->m_DesiredSize.x;
                contentSize.y = std::max(contentSize.y, child->m_DesiredSize.y);
            }
            else
            {
                contentSize.x = std::max(contentSize.x, child->m_Position.x + child->m_DesiredSize.x);
                contentSize.y = std::max(contentSize.y, child->m_Position.y + child->m_DesiredSize.y);
            }
        }

        if (visibleChildCount > 1u && m_LayoutMode == UILayoutMode::Vertical)
        {
            contentSize.y += m_Spacing * static_cast<float>(visibleChildCount - 1u);
        }
        else if (visibleChildCount > 1u && m_LayoutMode == UILayoutMode::Horizontal)
        {
            contentSize.x += m_Spacing * static_cast<float>(visibleChildCount - 1u);
        }

        contentSize.x += m_Padding.Left + m_Padding.Right;
        contentSize.y += m_Padding.Top + m_Padding.Bottom;

        math::Vec2 desired(
            std::max(m_PreferredSize.x, contentSize.x),
            std::max(m_PreferredSize.y, contentSize.y));
        m_DesiredSize = ClampSize(desired);
    }

    static float ResolveAlignedOffset(float available, float size, UIAlignment alignment)
    {
        if (alignment == UIAlignment::Center) { return std::max(0.0f, (available - size) * 0.5f); }
        if (alignment == UIAlignment::End) { return std::max(0.0f, available - size); }
        return 0.0f;
    }

    void ArrangeRecursive(const math::Vec2& position, const math::Vec2& arrangedSize)
    {
        m_Position = position;
        m_Size = ClampSize(arrangedSize);

        const float contentWidth = std::max(0.0f, m_Size.x - m_Padding.Left - m_Padding.Right);
        const float contentHeight = std::max(0.0f, m_Size.y - m_Padding.Top - m_Padding.Bottom);
        float cursorX = m_Padding.Left;
        float cursorY = m_Padding.Top;

        for (auto& child : m_Children)
        {
            if (child == nullptr || child->m_Visible == false)
            {
                continue;
            }

            math::Vec2 childSize = child->m_DesiredSize;
            math::Vec2 childPosition = child->m_Position;

            if (m_LayoutMode == UILayoutMode::Vertical)
            {
                if (child->m_HorizontalAlignment == UIAlignment::Stretch)
                {
                    childSize.x = child->ClampSize(math::Vec2(contentWidth, childSize.y)).x;
                }
                childPosition.x = m_Padding.Left + ResolveAlignedOffset(contentWidth, childSize.x, child->m_HorizontalAlignment);
                childPosition.y = cursorY;
                cursorY += childSize.y + m_Spacing;
            }
            else if (m_LayoutMode == UILayoutMode::Horizontal)
            {
                if (child->m_VerticalAlignment == UIAlignment::Stretch)
                {
                    childSize.y = child->ClampSize(math::Vec2(childSize.x, contentHeight)).y;
                }
                childPosition.x = cursorX;
                childPosition.y = m_Padding.Top + ResolveAlignedOffset(contentHeight, childSize.y, child->m_VerticalAlignment);
                cursorX += childSize.x + m_Spacing;
            }

            child->ArrangeRecursive(childPosition, childSize);
        }
    }

    void BuildDrawListRecursive(UIDrawList& drawList, const math::Vec2& parentAbsolutePosition) const
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
    math::Vec2 m_PreferredSize{};
    math::Vec2 m_DesiredSize{};
    math::Vec2 m_MinSize{};
    math::Vec2 m_MaxSize{ std::numeric_limits<float>::max(), std::numeric_limits<float>::max() };
    UIElement* m_Parent = nullptr;
    std::vector<Scope<UIElement>> m_Children;
    UIThickness m_Padding{};
    UILayoutMode m_LayoutMode = UILayoutMode::Absolute;
    UIAlignment m_HorizontalAlignment = UIAlignment::Start;
    UIAlignment m_VerticalAlignment = UIAlignment::Start;
    float m_Spacing = 0.0f;
    bool m_Visible = true;
};

} // namespace Raven
