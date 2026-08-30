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

enum class UILayoutMode { Absolute = 0, Vertical, Horizontal };
enum class UIAlignment { Start = 0, Center, End, Stretch };

struct UIThickness
{
    float Left = 0.0f;
    float Top = 0.0f;
    float Right = 0.0f;
    float Bottom = 0.0f;
    UIThickness() = default;
    explicit UIThickness(float uniform) : Left(uniform), Top(uniform), Right(uniform), Bottom(uniform) {}
    UIThickness(float horizontal, float vertical) : Left(horizontal), Top(vertical), Right(horizontal), Bottom(vertical) {}
};

// Raven UIのRetained Mode Treeを構成する基底要素です。
// MeasureはLeafからParentへ必要Sizeを集約し、ArrangeはParentからChildへ実配置を配ります。
// MarginはElement外側、PaddingはContainer内側の余白として明確に分離します。
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
        if (child == nullptr) { return nullptr; }
        child->m_Parent = this;
        UIElement* result = child.get();
        m_Children.push_back(std::move(child));
        InvalidateMeasure();
        return result;
    }

    void ClearChildren()
    {
        for (auto& child : m_Children) { if (child != nullptr) { child->m_Parent = nullptr; } }
        m_Children.clear();
        InvalidateMeasure();
    }

    void SetPosition(const math::Vec2& value) { m_Position = value; InvalidateArrange(); }
    void SetSize(const math::Vec2& value) { m_PreferredSize = ClampSize(value); m_Size = m_PreferredSize; InvalidateMeasure(); }
    void SetPreferredSize(const math::Vec2& value) { m_PreferredSize = ClampSize(value); InvalidateMeasure(); }
    void SetMinSize(const math::Vec2& value) { m_MinSize = math::Vec2(std::max(0.0f, value.x), std::max(0.0f, value.y)); InvalidateMeasure(); }
    void SetMaxSize(const math::Vec2& value) { m_MaxSize = math::Vec2(std::max(0.0f, value.x), std::max(0.0f, value.y)); InvalidateMeasure(); }
    void SetVisible(bool value) { if (m_Visible != value) { m_Visible = value; InvalidateMeasure(); } }
    void SetLayoutMode(UILayoutMode value) { if (m_LayoutMode != value) { m_LayoutMode = value; InvalidateMeasure(); } }
    void SetHorizontalAlignment(UIAlignment value) { if (m_HorizontalAlignment != value) { m_HorizontalAlignment = value; InvalidateArrange(); } }
    void SetVerticalAlignment(UIAlignment value) { if (m_VerticalAlignment != value) { m_VerticalAlignment = value; InvalidateArrange(); } }
    void SetPadding(const UIThickness& value) { m_Padding = value; InvalidateMeasure(); }
    void SetPadding(float value) { m_Padding = UIThickness(value); InvalidateMeasure(); }
    void SetMargin(const UIThickness& value) { m_Margin = value; InvalidateMeasure(); }
    void SetMargin(float value) { m_Margin = UIThickness(value); InvalidateMeasure(); }
    void SetSpacing(float value) { m_Spacing = std::max(0.0f, value); InvalidateMeasure(); }

    const math::Vec2& GetPosition() const { return m_Position; }
    const math::Vec2& GetSize() const { return m_Size; }
    const math::Vec2& GetPreferredSize() const { return m_PreferredSize; }
    const math::Vec2& GetDesiredSize() const { return m_DesiredSize; }
    const UIThickness& GetPadding() const { return m_Padding; }
    const UIThickness& GetMargin() const { return m_Margin; }
    bool IsVisible() const { return m_Visible; }
    bool IsMeasureDirty() const { return m_MeasureDirty; }
    bool IsArrangeDirty() const { return m_ArrangeDirty; }
    UIElement* GetParent() { return m_Parent; }
    const UIElement* GetParent() const { return m_Parent; }
    const std::vector<Scope<UIElement>>& GetChildren() const { return m_Children; }

    void BuildDrawList(UIDrawList& drawList)
    {
        // Dirty Flagにより、色だけ変わったframe等でLayout Tree全体を毎回再計算しません。
        // MeasureがDirtyならDesiredSizeが変化し得るためArrangeも必ず再実行します。
        if (m_MeasureDirty == true) { MeasureRecursive(); }
        if (m_ArrangeDirty == true) { ArrangeRecursive(m_Position, ResolveRootSize()); }
        BuildDrawListRecursive(drawList, math::Vec2(0.0f, 0.0f));
    }

protected:
    virtual void OnBuildDrawList(UIDrawList& drawList, const math::Vec2& absolutePosition) const
    {
        static_cast<void>(drawList); static_cast<void>(absolutePosition);
    }

private:
    math::Vec2 ClampSize(const math::Vec2& size) const
    {
        return math::Vec2(std::clamp(size.x, m_MinSize.x, m_MaxSize.x), std::clamp(size.y, m_MinSize.y, m_MaxSize.y));
    }

    math::Vec2 ResolveRootSize() const
    {
        math::Vec2 result = m_PreferredSize;
        if (result.x <= 0.0f) { result.x = m_DesiredSize.x; }
        if (result.y <= 0.0f) { result.y = m_DesiredSize.y; }
        return ClampSize(result);
    }

    math::Vec2 GetDesiredSizeWithMargin() const
    {
        return math::Vec2(m_DesiredSize.x + m_Margin.Left + m_Margin.Right, m_DesiredSize.y + m_Margin.Top + m_Margin.Bottom);
    }

    void InvalidateMeasure()
    {
        m_MeasureDirty = true;
        m_ArrangeDirty = true;
        // Childの必要Size変更は祖先ContainerのDesiredSizeへ波及するため、Measure Dirtyだけは上方向へ伝播します。
        if (m_Parent != nullptr && m_Parent->m_MeasureDirty == false) { m_Parent->InvalidateMeasure(); }
    }

    void InvalidateArrange()
    {
        m_ArrangeDirty = true;
        if (m_Parent != nullptr) { m_Parent->m_ArrangeDirty = true; }
    }

    void MeasureRecursive()
    {
        if (m_Visible == false) { m_DesiredSize = math::Vec2(0.0f, 0.0f); m_MeasureDirty = false; m_ArrangeDirty = true; return; }
        for (auto& child : m_Children) { if (child != nullptr && child->m_MeasureDirty == true) { child->MeasureRecursive(); } }

        math::Vec2 content(0.0f, 0.0f);
        uint32_t count = 0u;
        for (const auto& child : m_Children)
        {
            if (child == nullptr || child->m_Visible == false) { continue; }
            ++count;
            const math::Vec2 childOuter = child->GetDesiredSizeWithMargin();
            if (m_LayoutMode == UILayoutMode::Vertical) { content.x = std::max(content.x, childOuter.x); content.y += childOuter.y; }
            else if (m_LayoutMode == UILayoutMode::Horizontal) { content.x += childOuter.x; content.y = std::max(content.y, childOuter.y); }
            else { content.x = std::max(content.x, child->m_Position.x + childOuter.x); content.y = std::max(content.y, child->m_Position.y + childOuter.y); }
        }
        if (count > 1u && m_LayoutMode == UILayoutMode::Vertical) { content.y += m_Spacing * static_cast<float>(count - 1u); }
        else if (count > 1u && m_LayoutMode == UILayoutMode::Horizontal) { content.x += m_Spacing * static_cast<float>(count - 1u); }
        content.x += m_Padding.Left + m_Padding.Right;
        content.y += m_Padding.Top + m_Padding.Bottom;
        m_DesiredSize = ClampSize(math::Vec2(std::max(m_PreferredSize.x, content.x), std::max(m_PreferredSize.y, content.y)));
        m_MeasureDirty = false;
        m_ArrangeDirty = true;
    }

    static float ResolveAlignedOffset(float available, float size, UIAlignment alignment)
    {
        if (alignment == UIAlignment::Center) { return std::max(0.0f, (available - size) * 0.5f); }
        if (alignment == UIAlignment::End) { return std::max(0.0f, available - size); }
        return 0.0f;
    }

    void ArrangeRecursive(const math::Vec2& position, const math::Vec2& arrangedSize)
    {
        m_Position = position; m_Size = ClampSize(arrangedSize);
        const float contentWidth = std::max(0.0f, m_Size.x - m_Padding.Left - m_Padding.Right);
        const float contentHeight = std::max(0.0f, m_Size.y - m_Padding.Top - m_Padding.Bottom);
        float cursorX = m_Padding.Left; float cursorY = m_Padding.Top;

        for (auto& child : m_Children)
        {
            if (child == nullptr || child->m_Visible == false) { continue; }
            const float availableWidth = std::max(0.0f, contentWidth - child->m_Margin.Left - child->m_Margin.Right);
            const float availableHeight = std::max(0.0f, contentHeight - child->m_Margin.Top - child->m_Margin.Bottom);
            math::Vec2 childSize = child->m_DesiredSize;
            math::Vec2 childPosition = child->m_Position;

            if (m_LayoutMode == UILayoutMode::Vertical)
            {
                if (child->m_HorizontalAlignment == UIAlignment::Stretch) { childSize.x = child->ClampSize(math::Vec2(availableWidth, childSize.y)).x; }
                childPosition.x = m_Padding.Left + child->m_Margin.Left + ResolveAlignedOffset(availableWidth, childSize.x, child->m_HorizontalAlignment);
                childPosition.y = cursorY + child->m_Margin.Top;
                cursorY += childSize.y + child->m_Margin.Top + child->m_Margin.Bottom + m_Spacing;
            }
            else if (m_LayoutMode == UILayoutMode::Horizontal)
            {
                if (child->m_VerticalAlignment == UIAlignment::Stretch) { childSize.y = child->ClampSize(math::Vec2(childSize.x, availableHeight)).y; }
                childPosition.x = cursorX + child->m_Margin.Left;
                childPosition.y = m_Padding.Top + child->m_Margin.Top + ResolveAlignedOffset(availableHeight, childSize.y, child->m_VerticalAlignment);
                cursorX += childSize.x + child->m_Margin.Left + child->m_Margin.Right + m_Spacing;
            }
            child->ArrangeRecursive(childPosition, childSize);
        }
        m_ArrangeDirty = false;
    }

    void BuildDrawListRecursive(UIDrawList& drawList, const math::Vec2& parentAbsolutePosition) const
    {
        if (m_Visible == false) { return; }
        const math::Vec2 absolutePosition(parentAbsolutePosition.x + m_Position.x, parentAbsolutePosition.y + m_Position.y);
        OnBuildDrawList(drawList, absolutePosition);
        // Parentを先に描画し、Childを後から描画する単純なPainter's Orderです。ZIndex / Clipは後続で追加します。
        for (const auto& child : m_Children) { if (child != nullptr) { child->BuildDrawListRecursive(drawList, absolutePosition); } }
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
    UIThickness m_Margin{};
    UILayoutMode m_LayoutMode = UILayoutMode::Absolute;
    UIAlignment m_HorizontalAlignment = UIAlignment::Start;
    UIAlignment m_VerticalAlignment = UIAlignment::Start;
    float m_Spacing = 0.0f;
    bool m_Visible = true;
    bool m_MeasureDirty = true;
    bool m_ArrangeDirty = true;
};

} // namespace Raven
