#include "Raven/UI/Core/UIElement.h"
#include "Raven/UI/Core/UIContext.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace Raven
{

UIThickness::UIThickness() = default;
UIThickness::UIThickness(float uniform) : Left(uniform), Top(uniform), Right(uniform), Bottom(uniform) {}
UIThickness::UIThickness(float horizontal, float vertical) : Left(horizontal), Top(vertical), Right(horizontal), Bottom(vertical) {}

UIElement::UIElement() = default;
UIElement::~UIElement() = default;

UIElement* UIElement::AddChild(Scope<UIElement> child)
{
    if (child == nullptr)
    {
        return nullptr;
    }
    if (child->m_Parent != nullptr || child->m_Context != nullptr)
    {
        return nullptr;
    }

    child->m_Parent = this;
    child->SetContextRecursive(m_Context);
    UIElement* result = child.get();
    m_Children.push_back(std::move(child));
    NotifyBindingTreeChanged();
    InvalidateMeasure();
    return result;
}

Scope<UIElement> UIElement::DetachChild(UIElement* child)
{
    if (child == nullptr)
    {
        return nullptr;
    }

    auto iterator = std::find_if(m_Children.begin(), m_Children.end(), [child](const Scope<UIElement>& candidate)
    {
        return candidate.get() == child;
    });
    if (iterator == m_Children.end())
    {
        return nullptr;
    }

    if (m_Context != nullptr)
    {
        m_Context->OnSubtreeRemoving(iterator->get());
    }
    NotifyBindingTreeChanged();

    Scope<UIElement> detached = std::move(*iterator);
    m_Children.erase(iterator);
    detached->m_Parent = nullptr;
    detached->SetContextRecursive(nullptr);
    InvalidateMeasure();
    return detached;
}

bool UIElement::BringChildToFront(UIElement* child)
{
    auto iterator = std::find_if(m_Children.begin(), m_Children.end(),
        [child](const Scope<UIElement>& candidate)
        {
            return candidate.get() == child;
        });
    if (iterator == m_Children.end())
    {
        return false;
    }
    if (iterator + 1 != m_Children.end())
    {
        // Scopeの所有権は変えず、最後に描画される位置へ移動します。
        std::rotate(iterator, iterator + 1, m_Children.end());
        InvalidateArrange();
    }
    return true;
}

bool UIElement::RemoveChild(UIElement* child)
{
    Scope<UIElement> removed = DetachChild(child);
    return removed != nullptr;
}

void UIElement::ClearChildren()
{
    if (m_Children.empty())
    {
        return;
    }

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

    NotifyBindingTreeChanged();
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

bool UIElement::SetName(std::string name)
{
    if (name.find('/') != std::string::npos)
    {
        return false;
    }
    if (m_Name == name)
    {
        return true;
    }
    m_Name = std::move(name);
    NotifyBindingTreeChanged();
    return true;
}

uint64_t UIElement::GetTreeGeneration() const { return GetTreeRoot()->m_TreeGeneration; }

void UIElement::SetPosition(const math::Vec2& value) { m_Position = value; InvalidateArrange(); }
void UIElement::SetSize(const math::Vec2& value) { m_UsePreferredSizeDIP = false; m_PreferredSize = ClampSize(value); m_Size = m_PreferredSize; InvalidateMeasure(); }
void UIElement::SetPreferredSize(const math::Vec2& value) { m_UsePreferredSizeDIP = false; m_PreferredSize = ClampSize(value); InvalidateMeasure(); }
void UIElement::SetMinSize(const math::Vec2& value) { m_UseMinSizeDIP = false; m_MinSize = math::Vec2(std::max(0.0f, value.x), std::max(0.0f, value.y)); InvalidateMeasure(); }
void UIElement::SetMaxSize(const math::Vec2& value) { m_UseMaxSizeDIP = false; m_MaxSize = math::Vec2(std::max(0.0f, value.x), std::max(0.0f, value.y)); InvalidateMeasure(); }

void UIElement::SetVisible(bool value)
{
    if (m_Visible != value)
    {
        if (value == false && m_Context != nullptr)
        {
            // 非表示前ならFocus対象を探索できるため、OS側IMEもこの時点で終了します。
            m_Context->OnSubtreeRemoving(this);
        }
        m_Visible = value;
        InvalidateMeasure();
    }
}

void UIElement::SetLayoutMode(UILayoutMode value)
{
    if (m_LayoutMode != value)
    {
        m_LayoutMode = value;
        RefreshDPIMetrics();
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

void UIElement::SetPadding(const UIThickness& value) { m_UsePaddingDIP = false; m_Padding = value; InvalidateMeasure(); }
void UIElement::SetPadding(float value) { SetPadding(UIThickness(value)); }
void UIElement::SetMargin(const UIThickness& value) { m_UseMarginDIP = false; m_Margin = value; InvalidateMeasure(); }
void UIElement::SetMargin(float value) { SetMargin(UIThickness(value)); }
void UIElement::SetSpacing(float value) { m_UseSpacingDIP = false; m_Spacing = std::max(0.0f, value); InvalidateMeasure(); }

void UIElement::SetPreferredSizeDIP(const math::Vec2& value)
{
    m_PreferredSizeDIP = value;
    m_UsePreferredSizeDIP = true;
    RefreshDPIMetrics();
}

void UIElement::SetMinSizeDIP(const math::Vec2& value)
{
    m_MinSizeDIP = value;
    m_UseMinSizeDIP = true;
    RefreshDPIMetrics();
}

void UIElement::SetMaxSizeDIP(const math::Vec2& value)
{
    m_MaxSizeDIP = value;
    m_UseMaxSizeDIP = true;
    RefreshDPIMetrics();
}

void UIElement::SetPaddingDIP(const UIThickness& value)
{
    m_PaddingDIP = value;
    m_UsePaddingDIP = true;
    RefreshDPIMetrics();
}

void UIElement::SetMarginDIP(const UIThickness& value)
{
    m_MarginDIP = value;
    m_UseMarginDIP = true;
    RefreshDPIMetrics();
}

void UIElement::SetSpacingDIP(float value)
{
    m_SpacingDIP = value;
    m_UseSpacingDIP = true;
    RefreshDPIMetrics();
}

void UIElement::RefreshDPIMetrics()
{
    const float x = m_Context != nullptr ? m_Context->GetEffectiveScaleX() : 1.0f;
    const float y = m_Context != nullptr ? m_Context->GetEffectiveScaleY() : 1.0f;
    // 制約を先に更新し、PreferredSizeのClampに古いDPIのMin/Maxを使わないようにします。
    if (m_UseMinSizeDIP == true)
    {
        m_MinSize = math::Vec2(std::max(0.0f, m_MinSizeDIP.x * x),
            std::max(0.0f, m_MinSizeDIP.y * y));
    }
    if (m_UseMaxSizeDIP == true)
    {
        // MaxSizeの既定上限FLT_MAXは倍率を掛けるとInfinityになり得るため保持します。
        m_MaxSize = math::Vec2(
            m_MaxSizeDIP.x == std::numeric_limits<float>::max()
                ? std::numeric_limits<float>::max() : std::max(0.0f, m_MaxSizeDIP.x * x),
            m_MaxSizeDIP.y == std::numeric_limits<float>::max()
                ? std::numeric_limits<float>::max() : std::max(0.0f, m_MaxSizeDIP.y * y));
    }
    if (m_UsePreferredSizeDIP == true)
    {
        m_PreferredSize = ClampSize(math::Vec2(m_PreferredSizeDIP.x * x, m_PreferredSizeDIP.y * y));
    }
    if (m_UsePaddingDIP == true)
    {
        m_Padding.Left = m_PaddingDIP.Left * x;
        m_Padding.Top = m_PaddingDIP.Top * y;
        m_Padding.Right = m_PaddingDIP.Right * x;
        m_Padding.Bottom = m_PaddingDIP.Bottom * y;
    }
    if (m_UseMarginDIP == true)
    {
        m_Margin.Left = m_MarginDIP.Left * x;
        m_Margin.Top = m_MarginDIP.Top * y;
        m_Margin.Right = m_MarginDIP.Right * x;
        m_Margin.Bottom = m_MarginDIP.Bottom * y;
    }
    if (m_UseSpacingDIP == true)
    {
        // LayoutのVertical/Horizontal方向に合わせて軸を選択します。
        m_Spacing = std::max(0.0f, m_SpacingDIP * (m_LayoutMode == UILayoutMode::Horizontal ? x : y));
    }
    if (m_UsePreferredSizeDIP || m_UseMinSizeDIP || m_UseMaxSizeDIP ||
        m_UsePaddingDIP || m_UseMarginDIP || m_UseSpacingDIP)
    {
        InvalidateMeasure();
    }
}

void UIElement::RefreshDPIMetricsRecursive()
{
    RefreshDPIMetrics();
    for (auto& child : m_Children)
    {
        if (child != nullptr)
        {
            child->RefreshDPIMetricsRecursive();
        }
    }
}

void UIElement::SetAffectsParentMeasure(bool value)
{
    if (m_AffectsParentMeasure == value)
    {
        return;
    }

    m_AffectsParentMeasure = value;
    // 自身のDesiredSizeは変わりませんが、親が集約する対象が変わるため親側だけを再Measureします。
    if (m_Parent != nullptr)
    {
        m_Parent->InvalidateMeasure();
    }
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

void UIElement::SetHovered(bool value) { m_Hovered = value; }
void UIElement::SetPressed(bool value) { m_Pressed = value; }
void UIElement::HandleMouseEvent(UIMouseEvent& event) { OnMouseEvent(event); }

void UIElement::BuildDrawList(UIDrawList& drawList)
{
    if (m_MeasureDirty == true)
    {
        MeasureRecursive();
    }
    if (m_ArrangeDirty == true)
    {
        // 初回Measureの自然幅からRootの実幅を決め、幅依存の高さを親へ再集約します。
        ReflowForWidth(ResolveRootSize().x);
        ArrangeRecursive(m_Position, ResolveRootSize());
    }
    BuildDrawListRecursive(drawList, math::Vec2(0.0f, 0.0f), UITransform2D::Identity(), UIClipRect::Disabled());
}

math::Vec2 UIElement::OnMeasureContent() const
{
    return math::Vec2(0.0f, 0.0f);
}

math::Vec2 UIElement::OnMeasureContentForWidth(float availableWidth) const
{
    static_cast<void>(availableWidth);
    return OnMeasureContent();
}

void UIElement::OnMouseEvent(UIMouseEvent& event) { static_cast<void>(event); }
void UIElement::OnBuildDrawList(UIDrawList& drawList, const math::Vec2& absolutePosition) const
{
    static_cast<void>(drawList);
    static_cast<void>(absolutePosition);
}

math::Vec2 UIElement::ClampSize(const math::Vec2& size) const
{
    return math::Vec2(std::clamp(size.x, m_MinSize.x, m_MaxSize.x), std::clamp(size.y, m_MinSize.y, m_MaxSize.y));
}

math::Vec2 UIElement::ResolveRootSize() const
{
    math::Vec2 result = m_PreferredSize;
    if (result.x <= 0.0f) { result.x = m_DesiredSize.x; }
    if (result.y <= 0.0f) { result.y = m_DesiredSize.y; }
    return ClampSize(result);
}

math::Vec2 UIElement::GetDesiredSizeWithMargin() const
{
    return math::Vec2(m_DesiredSize.x + m_Margin.Left + m_Margin.Right, m_DesiredSize.y + m_Margin.Top + m_Margin.Bottom);
}

void UIElement::InvalidateMeasure()
{
    m_MeasureDirty = true;
    m_ArrangeDirty = true;
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

    // Measure参加可否に関係なくChild自身はMeasureします。
    // ScrollView Contentは実Content Sizeを保持したまま、親ViewportのDesiredSizeだけから除外する必要があります。
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
        if (child == nullptr || child->m_Visible == false || child->m_AffectsParentMeasure == false)
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

    const math::Vec2 intrinsic = OnMeasureContent();
    content.x = std::max(content.x, intrinsic.x);
    content.y = std::max(content.y, intrinsic.y);

    content.x += m_Padding.Left + m_Padding.Right;
    content.y += m_Padding.Top + m_Padding.Bottom;
    m_DesiredSize = ClampSize(math::Vec2(std::max(m_PreferredSize.x, content.x), std::max(m_PreferredSize.y, content.y)));
    m_MeasureDirty = false;
    m_ArrangeDirty = true;
}

void UIElement::ReflowForWidth(float arrangedWidth)
{
    if (m_Visible == false)
    {
        return;
    }

    const float contentWidth = std::max(0.0f,
        arrangedWidth - m_Padding.Left - m_Padding.Right);

    // 親の幅を先に配り、子の折り返し後の高さを得てから親のDesiredSizeを集約します。
    for (auto& child : m_Children)
    {
        if (child == nullptr || child->m_Visible == false)
        {
            continue;
        }

        float childWidth = child->m_DesiredSize.x;
        if (m_LayoutMode == UILayoutMode::Vertical &&
            child->m_HorizontalAlignment == UIAlignment::Stretch)
        {
            const float available = std::max(0.0f,
                contentWidth - child->m_Margin.Left - child->m_Margin.Right);
            childWidth = child->ClampSize(math::Vec2(available, child->m_DesiredSize.y)).x;
        }
        child->ReflowForWidth(childWidth);
    }

    math::Vec2 content(0.0f, 0.0f);
    std::uint32_t count = 0u;
    for (const auto& child : m_Children)
    {
        if (child == nullptr || child->m_Visible == false ||
            child->m_AffectsParentMeasure == false)
        {
            continue;
        }
        ++count;
        const math::Vec2 outer = child->GetDesiredSizeWithMargin();
        if (m_LayoutMode == UILayoutMode::Vertical)
        {
            content.x = std::max(content.x, outer.x);
            content.y += outer.y;
        }
        else if (m_LayoutMode == UILayoutMode::Horizontal)
        {
            content.x += outer.x;
            content.y = std::max(content.y, outer.y);
        }
        else
        {
            content.x = std::max(content.x, child->m_Position.x + outer.x);
            content.y = std::max(content.y, child->m_Position.y + outer.y);
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

    const math::Vec2 intrinsic = OnMeasureContentForWidth(contentWidth);
    content.x = std::max(content.x, intrinsic.x);
    content.y = std::max(content.y, intrinsic.y);
    content.x += m_Padding.Left + m_Padding.Right;
    content.y += m_Padding.Top + m_Padding.Bottom;
    m_DesiredSize = ClampSize(math::Vec2(
        std::max(m_PreferredSize.x, content.x),
        std::max(m_PreferredSize.y, content.y)));
}

float UIElement::ResolveAlignedOffset(float available, float size, UIAlignment alignment)
{
    if (alignment == UIAlignment::Center) { return std::max(0.0f, (available - size) * 0.5f); }
    if (alignment == UIAlignment::End) { return std::max(0.0f, available - size); }
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
                // ReflowForWidthが既に確定幅で子のDesiredSizeを再集約しています。
                // Arrange側で再Measureすると親と子の高さが異なるため、ここでは幅だけ確定します。
            }
            childPosition.x = m_Padding.Left + child->m_Margin.Left + ResolveAlignedOffset(availableWidth, childSize.x, child->m_HorizontalAlignment);
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
            childPosition.y = m_Padding.Top + child->m_Margin.Top + ResolveAlignedOffset(availableHeight, childSize.y, child->m_VerticalAlignment);
            cursorX += childSize.x + child->m_Margin.Left + child->m_Margin.Right + m_Spacing;
        }

        child->ArrangeRecursive(childPosition, childSize);
    }
    m_ArrangeDirty = false;
}

void UIElement::BuildDrawListRecursive(UIDrawList& drawList, const math::Vec2& parentAbsolutePosition, const UITransform2D& parentWorldTransform, const UIClipRect& inheritedClip) const
{
    if (m_Visible == false)
    {
        return;
    }

    const math::Vec2 absolutePosition(parentAbsolutePosition.x + m_Position.x, parentAbsolutePosition.y + m_Position.y);
    const math::Vec2 pivot(absolutePosition.x + m_Size.x * m_TransformPivot.x, absolutePosition.y + m_Size.y * m_TransformPivot.y);
    const UITransform2D localTransform = UITransform2D::CreateScaleRotation(pivot, m_Rotation, m_Scale);
    const UITransform2D worldTransform = UITransform2D::Combine(parentWorldTransform, localTransform);

    const std::size_t firstCommand = drawList.GetCommandCount();
    OnBuildDrawList(drawList, absolutePosition);
    drawList.ApplyTransform(firstCommand, worldTransform);
    UIClipRect childClip = inheritedClip;
    if (m_ClipChildren == true)
    {
        UIRect layoutRect;
        layoutRect.Min = absolutePosition;
        layoutRect.Max = math::Vec2(absolutePosition.x + m_Size.x, absolutePosition.y + m_Size.y);
        const UIRect transformedBounds = worldTransform.TransformRectBounds(layoutRect);
        childClip = UIClipRect::Intersect(inheritedClip, transformedBounds);
    }

    // 自身の描画と子の描画は独立してClipを選択します。入力欄は自身もClipします。
    drawList.ApplyClip(firstCommand, m_ClipSelf == true ? childClip : inheritedClip);

    for (const auto& child : m_Children)
    {
        if (child != nullptr)
        {
            child->BuildDrawListRecursive(drawList, absolutePosition, worldTransform, childClip);
        }
    }
}

void UIElement::SetContextRecursive(UIContext* context)
{
    UIContext* previous = m_Context;
    if (previous != context)
    {
        // Detach時も旧Contextが有効な間にWidget固有のPopup等を解除します。
        OnContextChanged(previous, context);
    }
    m_Context = context;
    RefreshDPIMetrics();
    for (auto& child : m_Children)
    {
        if (child != nullptr)
        {
            child->SetContextRecursive(context);
        }
    }
}

void UIElement::NotifyBindingTreeChanged()
{
    UIElement* root = GetTreeRoot();
    if (root->m_TreeGeneration == std::numeric_limits<uint64_t>::max())
    {
        root->m_TreeGeneration = 1u;
    }
    else
    {
        ++root->m_TreeGeneration;
    }
}

UIElement* UIElement::GetTreeRoot()
{
    UIElement* current = this;
    while (current->m_Parent != nullptr) { current = current->m_Parent; }
    return current;
}

const UIElement* UIElement::GetTreeRoot() const
{
    const UIElement* current = this;
    while (current->m_Parent != nullptr) { current = current->m_Parent; }
    return current;
}

} // namespace Raven
