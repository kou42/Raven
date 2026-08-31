#include "Raven/UI/Core/UIElement.h"
#include "Raven/UI/Core/UIContext.h"

#include <algorithm>
#include <utility>

namespace Raven
{

UIThickness::UIThickness() = default;

UIThickness::UIThickness(float uniform)
    : Left(uniform), Top(uniform), Right(uniform), Bottom(uniform)
{
}

UIThickness::UIThickness(float horizontal, float vertical)
    : Left(horizontal), Top(vertical), Right(horizontal), Bottom(vertical)
{
}

UIElement::UIElement() = default;
UIElement::~UIElement() = default;

UIElement* UIElement::AddChild(Scope<UIElement> child)
{
    if (child == nullptr)
    {
        return nullptr;
    }

    // 1つのElementを複数Parentへ接続するとParent chainとContext所属が矛盾します。
    // Tree外で構築したSubtreeだけをAddChild()できるようにし、移動はDetachChild()経由で明示します。
    if (child->m_Parent != nullptr || child->m_Context != nullptr)
    {
        return nullptr;
    }

    child->m_Parent = this;
    child->SetContextRecursive(m_Context);
    UIElement* result = child.get();
    m_Children.push_back(std::move(child));
    InvalidateMeasure();
    return result;
}

Scope<UIElement> UIElement::DetachChild(UIElement* child)
{
    if (child == nullptr)
    {
        return nullptr;
    }

    auto iterator = std::find_if(
        m_Children.begin(),
        m_Children.end(),
        [child](const Scope<UIElement>& candidate)
        {
            return candidate.get() == child;
        });

    if (iterator == m_Children.end())
    {
        return nullptr;
    }

    // Contextが保持するraw pointerはSubtreeをTreeから外す前に必ず掃除します。
    // Capture中ならCancel EventをParent chainが有効な状態でBubbleさせてから切り離します。
    if (m_Context != nullptr)
    {
        m_Context->OnSubtreeRemoving(iterator->get());
    }

    Scope<UIElement> detached = std::move(*iterator);
    m_Children.erase(iterator);

    detached->m_Parent = nullptr;
    detached->SetContextRecursive(nullptr);
    InvalidateMeasure();
    return detached;
}

bool UIElement::RemoveChild(UIElement* child)
{
    Scope<UIElement> removed = DetachChild(child);
    return removed != nullptr;
}

void UIElement::ClearChildren()
{
    // Scopeを破棄してからContext側のraw pointerを掃除することはできません。
    // Capture中Widgetを含むSubtreeが消える場合は、Parent chainがまだ有効なこの時点で
    // Cancel Eventを配送し、Hover / Pressedも含めたInteraction Stateを先に終了します。
    if (m_Context != nullptr)
    {
        for (auto& child : m_Children)
        {
            if (child != nullptr)
            {
                m_Context->OnSubtreeRemoving(child.get());
            }
        }
    }

    for (auto& child : m_Children)
    {
        if (child != nullptr)
        {
            child->m_Parent = nullptr;
            child->SetContextRecursive(nullptr);
        }
    }

    m_Children.clear();
    InvalidateMeasure();
}

void UIElement::SetPosition(const math::Vec2& value)
{
    m_Position = value;
    InvalidateArrange();
}

void UIElement::SetSize(const math::Vec2& value)
{
    m_PreferredSize = ClampSize(value);
    m_Size = m_PreferredSize;
    InvalidateMeasure();
}

void UIElement::SetPreferredSize(const math::Vec2& value)
{
    m_PreferredSize = ClampSize(value);
    InvalidateMeasure();
}

void UIElement::SetMinSize(const math::Vec2& value)
{
    m_MinSize = math::Vec2(std::max(0.0f, value.x), std::max(0.0f, value.y));
    InvalidateMeasure();
}

void UIElement::SetMaxSize(const math::Vec2& value)
{
    m_MaxSize = math::Vec2(std::max(0.0f, value.x), std::max(0.0f, value.y));
    InvalidateMeasure();
}

void UIElement::SetVisible(bool value)
{
    if (m_Visible != value)
    {
        m_Visible = value;
        InvalidateMeasure();
    }
}

void UIElement::SetLayoutMode(UILayoutMode value)
{
    if (m_LayoutMode != value)
    {
        m_LayoutMode = value;
        InvalidateMeasure();
    }
}

void UIElement::SetHorizontalAlignment(UIAlignment value)
{
    if (m_HorizontalAlignment != value)
    {
        m_HorizontalAlignment = value;
        InvalidateArrange();
    }
}

void UIElement::SetVerticalAlignment(UIAlignment value)
{
    if (m_VerticalAlignment != value)
    {
        m_VerticalAlignment = value;
        InvalidateArrange();
    }
}

void UIElement::SetPadding(const UIThickness& value)
{
    m_Padding = value;
    InvalidateMeasure();
}

void UIElement::SetPadding(float value)
{
    m_Padding = UIThickness(value);
    InvalidateMeasure();
}

void UIElement::SetMargin(const UIThickness& value)
{
    m_Margin = value;
    InvalidateMeasure();
}

void UIElement::SetMargin(float value)
{
    m_Margin = UIThickness(value);
    InvalidateMeasure();
}

void UIElement::SetSpacing(float value)
{
    m_Spacing = std::max(0.0f, value);
    InvalidateMeasure();
}

const math::Vec2& UIElement::GetPosition() const { return m_Position; }
const math::Vec2& UIElement::GetSize() const { return m_Size; }
const math::Vec2& UIElement::GetPreferredSize() const { return m_PreferredSize; }
const math::Vec2& UIElement::GetDesiredSize() const { return m_DesiredSize; }
const UIThickness& UIElement::GetPadding() const { return m_Padding; }
const UIThickness& UIElement::GetMargin() const { return m_Margin; }
bool UIElement::IsVisible() const { return m_Visible; }
bool UIElement::IsHovered() const { return m_Hovered; }
bool UIElement::IsPressed() const { return m_Pressed; }
bool UIElement::IsMeasureDirty() const { return m_MeasureDirty; }
bool UIElement::IsArrangeDirty() const { return m_ArrangeDirty; }
UIElement* UIElement::GetParent() { return m_Parent; }
const UIElement* UIElement::GetParent() const { return m_Parent; }
const std::vector<Scope<UIElement>>& UIElement::GetChildren() const { return m_Children; }

void UIElement::SetHovered(bool value)
{
    m_Hovered = value;
}

void UIElement::SetPressed(bool value)
{
    m_Pressed = value;
}

void UIElement::HandleMouseEvent(UIMouseEvent& event)
{
    OnMouseEvent(event);
}

void UIElement::BuildDrawList(UIDrawList& drawList)
{
    // Dirty Flagにより、色だけ変わったframe等でLayout Tree全体を毎回再計算しません。
    // MeasureがDirtyならDesiredSizeが変化し得るためArrangeも必ず再実行します。
    if (m_MeasureDirty == true)
    {
        MeasureRecursive();
    }

    if (m_ArrangeDirty == true)
    {
        ArrangeRecursive(m_Position, ResolveRootSize());
    }

    BuildDrawListRecursive(drawList, math::Vec2(0.0f, 0.0f));
}

void UIElement::OnMouseEvent(UIMouseEvent& event)
{
    static_cast<void>(event);
}

void UIElement::OnBuildDrawList(UIDrawList& drawList, const math::Vec2& absolutePosition) const
{
    static_cast<void>(drawList);
    static_cast<void>(absolutePosition);
}

math::Vec2 UIElement::ClampSize(const math::Vec2& size) const
{
    return math::Vec2(
        std::clamp(size.x, m_MinSize.x, m_MaxSize.x),
        std::clamp(size.y, m_MinSize.y, m_MaxSize.y));
}

math::Vec2 UIElement::ResolveRootSize() const
{
    math::Vec2 result = m_PreferredSize;
    if (result.x <= 0.0f)
    {
        result.x = m_DesiredSize.x;
    }
    if (result.y <= 0.0f)
    {
        result.y = m_DesiredSize.y;
    }
    return ClampSize(result);
}

math::Vec2 UIElement::GetDesiredSizeWithMargin() const
{
    return math::Vec2(
        m_DesiredSize.x + m_Margin.Left + m_Margin.Right,
        m_DesiredSize.y + m_Margin.Top + m_Margin.Bottom);
}

void UIElement::InvalidateMeasure()
{
    m_MeasureDirty = true;
    m_ArrangeDirty = true;

    // Childの必要Size変更は祖先ContainerのDesiredSizeへ波及するため、Measure Dirtyだけは上方向へ伝播します。
    if (m_Parent != nullptr && m_Parent->m_MeasureDirty == false)
    {
        m_Parent->InvalidateMeasure();
    }
}

void UIElement::InvalidateArrange()
{
    m_ArrangeDirty = true;
    if (m_Parent != nullptr)
    {
        m_Parent->m_ArrangeDirty = true;
    }
}

void UIElement::MeasureRecursive()
{
    if (m_Visible == false)
    {
        m_DesiredSize = math::Vec2(0.0f, 0.0f);
        m_MeasureDirty = false;
        m_ArrangeDirty = true;
        return;
    }

    for (auto& child : m_Children)
    {
        if (child != nullptr && child->m_MeasureDirty == true)
        {
            child->MeasureRecursive();
        }
    }

    math::Vec2 content(0.0f, 0.0f);
    uint32_t count = 0u;
    for (const auto& child : m_Children)
    {
        if (child == nullptr || child->m_Visible == false)
        {
            continue;
        }

        ++count;
        const math::Vec2 childOuter = child->GetDesiredSizeWithMargin();
        if (m_LayoutMode == UILayoutMode::Vertical)
        {
            content.x = std::max(content.x, childOuter.x);
            content.y += childOuter.y;
        }
        else if (m_LayoutMode == UILayoutMode::Horizontal)
        {
            content.x += childOuter.x;
            content.y = std::max(content.y, childOuter.y);
        }
        else
        {
            content.x = std::max(content.x, child->m_Position.x + childOuter.x);
            content.y = std::max(content.y, child->m_Position.y + childOuter.y);
        }
    }

    if (count > 1u && m_LayoutMode == UILayoutMode::Vertical)
    {
        content.y += m_Spacing * static_cast<float>(count - 1u);
    }
    else if (count > 1u && m_LayoutMode == UILayoutMode::Horizontal)
    {
        content.x += m_Spacing * static_cast<float>(count - 1u);
    }

    content.x += m_Padding.Left + m_Padding.Right;
    content.y += m_Padding.Top + m_Padding.Bottom;
    m_DesiredSize = ClampSize(math::Vec2(
        std::max(m_PreferredSize.x, content.x),
        std::max(m_PreferredSize.y, content.y)));
    m_MeasureDirty = false;
    m_ArrangeDirty = true;
}

float UIElement::ResolveAlignedOffset(float available, float size, UIAlignment alignment)
{
    if (alignment == UIAlignment::Center)
    {
        return std::max(0.0f, (available - size) * 0.5f);
    }
    if (alignment == UIAlignment::End)
    {
        return std::max(0.0f, available - size);
    }
    return 0.0f;
}

void UIElement::ArrangeRecursive(const math::Vec2& position, const math::Vec2& arrangedSize)
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

        const float availableWidth = std::max(0.0f, contentWidth - child->m_Margin.Left - child->m_Margin.Right);
        const float availableHeight = std::max(0.0f, contentHeight - child->m_Margin.Top - child->m_Margin.Bottom);
        math::Vec2 childSize = child->m_DesiredSize;
        math::Vec2 childPosition = child->m_Position;

        if (m_LayoutMode == UILayoutMode::Vertical)
        {
            if (child->m_HorizontalAlignment == UIAlignment::Stretch)
            {
                childSize.x = child->ClampSize(math::Vec2(availableWidth, childSize.y)).x;
            }
            childPosition.x = m_Padding.Left + child->m_Margin.Left +
                ResolveAlignedOffset(availableWidth, childSize.x, child->m_HorizontalAlignment);
            childPosition.y = cursorY + child->m_Margin.Top;
            cursorY += childSize.y + child->m_Margin.Top + child->m_Margin.Bottom + m_Spacing;
        }
        else if (m_LayoutMode == UILayoutMode::Horizontal)
        {
            if (child->m_VerticalAlignment == UIAlignment::Stretch)
            {
                childSize.y = child->ClampSize(math::Vec2(childSize.x, availableHeight)).y;
            }
            childPosition.x = cursorX + child->m_Margin.Left;
            childPosition.y = m_Padding.Top + child->m_Margin.Top +
                ResolveAlignedOffset(availableHeight, childSize.y, child->m_VerticalAlignment);
            cursorX += childSize.x + child->m_Margin.Left + child->m_Margin.Right + m_Spacing;
        }

        child->ArrangeRecursive(childPosition, childSize);
    }

    m_ArrangeDirty = false;
}

void UIElement::BuildDrawListRecursive(UIDrawList& drawList, const math::Vec2& parentAbsolutePosition) const
{
    if (m_Visible == false)
    {
        return;
    }

    const math::Vec2 absolutePosition(
        parentAbsolutePosition.x + m_Position.x,
        parentAbsolutePosition.y + m_Position.y);
    OnBuildDrawList(drawList, absolutePosition);

    // Parentを先に描画し、Childを後から描画する単純なPainter's Orderです。ZIndex / Clipは後続で追加します。
    for (const auto& child : m_Children)
    {
        if (child != nullptr)
        {
            child->BuildDrawListRecursive(drawList, absolutePosition);
        }
    }
}

void UIElement::SetContextRecursive(UIContext* context)
{
    m_Context = context;

    // Context所属はParent/Child関係と同じLifetime境界で管理します。
    // 後から構築済みSubtreeをAddChild()した場合も、全Descendantが同じContextへ所属する必要があります。
    for (auto& child : m_Children)
    {
        if (child != nullptr)
        {
            child->SetContextRecursive(context);
        }
    }
}

} // namespace Raven
