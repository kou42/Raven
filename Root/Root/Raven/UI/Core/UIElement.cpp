#include "Raven/UI/Core/UIElement.h"
#include "Raven/UI/Core/UIContext.h"

#include <algorithm>
#include <chrono>
#include <limits>
#include <utility>
#include <typeinfo>

namespace Raven
{
namespace
{
bool SameThickness(const UIThickness& left, const UIThickness& right)
{
    return left.Left == right.Left && left.Top == right.Top &&
        left.Right == right.Right && left.Bottom == right.Bottom;
}
} // namespace


// SnapshotはElement自身が所有し、Disable/破棄時に解放します。
// 親やContextへの非所有pointerをCache内へ保存しないため、移譲/破棄順に依存しません。
struct UIElement::DrawListCacheState
{
    UIDrawList Commands;
    bool Valid = false;
    bool EligibilityKnown = false;
    bool Eligible = false;
    math::Vec2 AbsolutePosition{};
    math::Vec2 Size{};
    UITransform2D WorldTransform{};
    UIClipRect InheritedClip{};
    math::Vec4 VisualColor{};
};

void UIElement::SetDrawListCacheEnabled(bool enabled)
{
    if (enabled == IsDrawListCacheEnabled())
    {
        return;
    }
    if (enabled == true)
    {
        m_DrawListCache = CreateScope<DrawListCacheState>();
    }
    else
    {
        m_DrawListCache.reset();
    }
    // 祖先のSnapshotに隠れて新しいCache方針が反映されないことを防ぎます。
    InvalidateGeometry();
}
bool UIElement::IsDrawListCacheEnabled() const { return m_DrawListCache != nullptr; }
std::size_t UIElement::GetDrawListCacheRetainedBytes() const
{
    return m_DrawListCache != nullptr
        ? sizeof(DrawListCacheState) + m_DrawListCache->Commands.GetRetainedAllocationBytes() : 0u;
}
std::size_t UIElement::GetDrawListCacheCommandCount() const
{
    return m_DrawListCache != nullptr ? m_DrawListCache->Commands.GetCommandCount() : 0u;
}
bool UIElement::SupportsDrawListCache() const
{
    return typeid(*this) == typeid(UIElement);
}
bool UIElement::IsSubtreeDrawListCacheSupported() const
{
    if (SupportsDrawListCache() == false)
    {
        return false;
    }
    for (const auto& child : m_Children)
    {
        if (child != nullptr && child->IsSubtreeDrawListCacheSupported() == false)
        {
            return false;
        }
    }
    return true;
}
void UIElement::InvalidateDrawListCache()
{
    if (m_DrawListCache != nullptr)
    {
        m_DrawListCache->Valid = false;
    }
}

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

void UIElement::SetPosition(const math::Vec2& value)
{
    // DIP→Pixelの切替と、Arrange済み値への即時再指定は同値でも意味を持ちます。
    if (m_UsePositionDIP == false && m_RequestedPosition == value && m_Position == value)
    {
        return;
    }
    m_UsePositionDIP = false;
    m_RequestedPosition = value;
    m_Position = value;
    InvalidateArrange();
}
void UIElement::SetSize(const math::Vec2& value)
{
    const auto next = ClampSize(value);
    if (m_UsePreferredSizeDIP == false && m_PreferredSize == next && m_Size == next)
    {
        return;
    }
    m_UsePreferredSizeDIP = false;
    m_PreferredSize = next;
    m_Size = next;
    InvalidateMeasure();
}
void UIElement::SetPreferredSize(const math::Vec2& value)
{
    const auto next = ClampSize(value);
    if (m_UsePreferredSizeDIP == false && m_PreferredSize == next)
    {
        return;
    }
    m_UsePreferredSizeDIP = false;
    m_PreferredSize = next;
    InvalidateMeasure();
}

void UIElement::SetMinSize(const math::Vec2& value)
{
    const math::Vec2 next(std::max(0.0f, value.x), std::max(0.0f, value.y));
    if (m_UseMinSizeDIP == false && m_MinSize == next)
    {
        return;
    }
    m_UseMinSizeDIP = false;
    m_MinSize = next;
    InvalidateMeasure();
}

void UIElement::SetMaxSize(const math::Vec2& value)
{
    const math::Vec2 next(std::max(0.0f, value.x), std::max(0.0f, value.y));
    if (m_UseMaxSizeDIP == false && m_MaxSize == next)
    {
        return;
    }
    m_UseMaxSizeDIP = false;
    m_MaxSize = next;
    InvalidateMeasure();
}


void UIElement::SetRotation(float value)
{
    const auto next = value;
    if (m_Rotation == next)
    {
        return;
    }
    m_Rotation = next;
    InvalidateVisual(true, false, true);
}

void UIElement::SetScale(const math::Vec2& value)
{
    const auto next = value;
    if (m_Scale == next)
    {
        return;
    }
    m_Scale = next;
    InvalidateVisual(true, false, true);
}

void UIElement::SetTransformPivot(const math::Vec2& value)
{
    const auto next = value;
    if (m_TransformPivot == next)
    {
        return;
    }
    m_TransformPivot = next;
    InvalidateVisual(true, false, true);
}

void UIElement::SetClipChildren(bool value)
{
    const auto next = value;
    if (m_ClipChildren == next)
    {
        return;
    }
    m_ClipChildren = next;
    InvalidateVisual(true, false, true);
}

void UIElement::SetClipSelf(bool value)
{
    const auto next = value;
    if (m_ClipSelf == next)
    {
        return;
    }
    m_ClipSelf = next;
    InvalidateVisual(true, false, false);
}

void UIElement::SetOpacity(float value)
{
    const auto next = std::clamp(value, 0.0f, 1.0f);
    if (m_Opacity == next)
    {
        return;
    }
    m_Opacity = next;
    InvalidateVisual(false, false, true);
}

void UIElement::SetTintColor(const math::Vec4& value)
{
    const auto next = value;
    if (m_TintColor == next)
    {
        return;
    }
    m_TintColor = next;
    InvalidateVisual(false, false, true);
}

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

void UIElement::SetPadding(const UIThickness& value)
{
    if (m_UsePaddingDIP == false && SameThickness(m_Padding, value))
    {
        return;
    }
    m_UsePaddingDIP = false;
    m_Padding = value;
    InvalidateMeasure();
}
void UIElement::SetPadding(float value) { SetPadding(UIThickness(value)); }
void UIElement::SetMargin(const UIThickness& value)
{
    if (m_UseMarginDIP == false && SameThickness(m_Margin, value))
    {
        return;
    }
    m_UseMarginDIP = false;
    m_Margin = value;
    InvalidateMeasure();
}
void UIElement::SetMargin(float value) { SetMargin(UIThickness(value)); }
void UIElement::SetSpacing(float value)
{
    const float next = std::max(0.0f, value);
    if (m_UseSpacingDIP == false && m_Spacing == next)
    {
        return;
    }
    m_UseSpacingDIP = false;
    m_Spacing = next;
    InvalidateMeasure();
}

void UIElement::SetPositionDIP(const math::Vec2& value)
{
    if (m_UsePositionDIP == true && m_PositionDIP == value)
    {
        return;
    }
    m_PositionDIP = value;
    m_UsePositionDIP = true;
    RefreshDPIMetrics();
}

void UIElement::SetPreferredSizeDIP(const math::Vec2& value)
{
    if (m_UsePreferredSizeDIP == true && m_PreferredSizeDIP == value)
    {
        return;
    }
    m_PreferredSizeDIP = value;
    m_UsePreferredSizeDIP = true;
    RefreshDPIMetrics();
}

void UIElement::SetMinSizeDIP(const math::Vec2& value)
{
    if (m_UseMinSizeDIP == true && m_MinSizeDIP == value)
    {
        return;
    }
    m_MinSizeDIP = value;
    m_UseMinSizeDIP = true;
    RefreshDPIMetrics();
}

void UIElement::SetMaxSizeDIP(const math::Vec2& value)
{
    if (m_UseMaxSizeDIP == true && m_MaxSizeDIP == value)
    {
        return;
    }
    m_MaxSizeDIP = value;
    m_UseMaxSizeDIP = true;
    RefreshDPIMetrics();
}

void UIElement::SetPaddingDIP(const UIThickness& value)
{
    if (m_UsePaddingDIP == true && SameThickness(m_PaddingDIP, value))
    {
        return;
    }
    m_PaddingDIP = value;
    m_UsePaddingDIP = true;
    RefreshDPIMetrics();
}

void UIElement::SetMarginDIP(const UIThickness& value)
{
    if (m_UseMarginDIP == true && SameThickness(m_MarginDIP, value))
    {
        return;
    }
    m_MarginDIP = value;
    m_UseMarginDIP = true;
    RefreshDPIMetrics();
}

void UIElement::SetSpacingDIP(float value)
{
    if (m_UseSpacingDIP == true && m_SpacingDIP == value)
    {
        return;
    }
    m_SpacingDIP = value;
    m_UseSpacingDIP = true;
    RefreshDPIMetrics();
}

void UIElement::RefreshDPIMetrics()
{
    const float x = m_Context != nullptr ? m_Context->GetEffectiveScaleX() : 1.0f;
    const float y = m_Context != nullptr ? m_Context->GetEffectiveScaleY() : 1.0f;
    if (m_UsePositionDIP == true)
    {
        m_RequestedPosition = math::Vec2(m_PositionDIP.x * x, m_PositionDIP.y * y);
        // 実配置位置はArrangeが確定します。ここでは指定位置だけを更新します。
        InvalidateArrange();
    }
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
    InvalidateDrawListCache();
    m_PaintDirty = true;
    m_GeometryDirty = true;
    m_StyleDirty = true;
    RefreshDPIMetrics();
    OnDPIScaleChanged();
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
bool UIElement::AcceptsTextInput() const { return false; }
UIElement* UIElement::GetParent() { return m_Parent; }
const UIElement* UIElement::GetParent() const { return m_Parent; }
const std::vector<Scope<UIElement>>& UIElement::GetChildren() const { return m_Children; }

void UIElement::SetHovered(bool value)
{
    if (m_Hovered != value)
    {
        m_Hovered = value;
        InvalidatePaint();
    }
}
void UIElement::SetPressed(bool value)
{
    if (m_Pressed != value)
    {
        m_Pressed = value;
        InvalidatePaint();
    }
}
void UIElement::HandleMouseEvent(UIMouseEvent& event) { InvalidateGeometry(); OnMouseEvent(event); }

void UIElement::BuildDrawList(UIDrawList& drawList, double* layoutMilliseconds, UIDrawListCacheMetrics* cacheMetrics)
{
    const auto layoutStarted = std::chrono::steady_clock::now();
    if (m_MeasureDirty == true)
    {
        MeasureRecursive();
    }
    if (m_ArrangeDirty == true)
    {
        // 初回Measureの自然幅からRootの実幅を決め、幅依存の高さを親へ再集約します。
        ReflowForWidth(ResolveRootSize().x);
        ArrangeRecursive(m_RequestedPosition, ResolveRootSize());
    }
    if (layoutMilliseconds != nullptr)
    {
        *layoutMilliseconds = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - layoutStarted).count();
    }
    BuildDrawListRecursive(drawList, math::Vec2(0.0f, 0.0f), UITransform2D::Identity(), UIClipRect::Disabled(), cacheMetrics);
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

bool UIElement::HandleDragDropEvent(UIDragDropEvent& event)
{
    InvalidateGeometry();
    return OnDragDropEvent(event);
}

void UIElement::HandleKeyEvent(UIKeyEvent& event)
{
    InvalidateGeometry();
    OnKeyEvent(event);
}

void UIElement::HandleCharacterEvent(UICharacterEvent& event)
{
    InvalidateGeometry();
    OnCharacterEvent(event);
}

void UIElement::HandleIMEEvent(UIIMEEvent& event)
{
    InvalidateGeometry();
    OnIMEEvent(event);
}

void UIElement::SetFocused(bool value)
{
    if (m_Focused == value)
    {
        return;
    }
    m_Focused = value;
    // Focus遷移の通知はUIContextの所有権更新後にWidgetへ伝えます。
    InvalidateGeometry();
    OnFocusChanged(value);
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

// Paintは色/Opacity、Geometryは形状/位置/Clip、StyleはTheme依存の再解決を表します。
// 祖先へは集約だけを行い、兄弟を無効化しません。継承Visual変更だけ子孫へ明示伝播します。
void UIElement::InvalidateVisualSubtree(bool geometry, bool style)
{
    InvalidateDrawListCache();
    m_PaintDirty = true;
    m_GeometryDirty = m_GeometryDirty || geometry;
    m_StyleDirty = m_StyleDirty || style;
    for (auto& child : m_Children)
    {
        child->InvalidateVisualSubtree(geometry, style);
    }
}
void UIElement::InvalidateVisual(bool geometry, bool style, bool descendants)
{
    for (UIElement* current = this; current != nullptr; current = current->m_Parent)
    {
        current->InvalidateDrawListCache();
        current->m_PaintDirty = true;
        current->m_GeometryDirty = current->m_GeometryDirty || geometry;
        current->m_StyleDirty = current->m_StyleDirty || style;
    }
    if (descendants == true)
    {
        for (auto& child : m_Children)
        {
            child->InvalidateVisualSubtree(geometry, style);
        }
    }
}
void UIElement::InvalidatePaint() { InvalidateVisual(false, false, false); }
void UIElement::InvalidateGeometry() { InvalidateVisual(true, false, false); }
void UIElement::InvalidateStyle() { InvalidateVisual(true, true, true); }
bool UIElement::IsPaintDirty() const { return m_PaintDirty; }
bool UIElement::IsGeometryDirty() const { return m_GeometryDirty; }
bool UIElement::IsStyleDirty() const { return m_StyleDirty; }

void UIElement::InvalidateMeasure()
{
    // 深い階層でもRootがLayout開始を判断できるよう全祖先へ伝えます。
    for (UIElement* current = this; current != nullptr; current = current->m_Parent)
    {
        current->InvalidateDrawListCache();
        current->m_MeasureDirty = true;
        current->m_ArrangeDirty = true;
        current->m_GeometryDirty = true;
        current->m_PaintDirty = true;
    }
}
void UIElement::InvalidateArrange()
{
    for (UIElement* current = this; current != nullptr; current = current->m_Parent)
    {
        current->InvalidateDrawListCache();
        current->m_ArrangeDirty = true;
        current->m_GeometryDirty = true;
        current->m_PaintDirty = true;
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
        if ((m_LayoutMode == UILayoutMode::Vertical || m_LayoutMode == UILayoutMode::Absolute) &&
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
    const math::Vec2 nextSize = ClampSize(arrangedSize);
    // 同値ArrangeではSnapshotを保持し、実配置が変わる場合だけ失効させます。
    if ((m_Position == position && m_Size == nextSize) == false)
    {
        InvalidateGeometry();
    }
    m_Position = position;
    m_Size = nextSize;

    const float contentWidth = std::max(0.0f, m_Size.x - m_Padding.Left - m_Padding.Right);
    const float contentHeight = std::max(0.0f, m_Size.y - m_Padding.Top - m_Padding.Bottom);
    float cursorX = m_Padding.Left;
    float cursorY = m_Padding.Top;

    // Verticalの主軸Stretchは、固定高さの兄弟・Margin・Spacingを除いた残りを
    // 均等配分します。Tab HeaderとContentのように自然サイズとViewportを分離し、
    // 本文が空/巨大な場合も親の確定寸法へ収めるために必要です。
    float verticalStretchHeight = 0.0f;
    if (m_LayoutMode == UILayoutMode::Vertical)
    {
        float fixedHeight = 0.0f;
        std::size_t visibleCount = 0u;
        std::size_t stretchCount = 0u;
        for (const auto& child : m_Children)
        {
            if (child == nullptr || child->m_Visible == false)
            {
                continue;
            }
            ++visibleCount;
            fixedHeight += child->m_Margin.Top + child->m_Margin.Bottom;
            if (child->m_VerticalAlignment == UIAlignment::Stretch)
            {
                ++stretchCount;
            }
            else
            {
                fixedHeight += child->m_DesiredSize.y;
            }
        }
        if (visibleCount > 1u)
        {
            fixedHeight += m_Spacing * static_cast<float>(visibleCount - 1u);
        }
        if (stretchCount > 0u)
        {
            verticalStretchHeight = std::max(0.0f, contentHeight - fixedHeight) /
                static_cast<float>(stretchCount);
        }
    }

    for (auto& child : m_Children)
    {
        if (child == nullptr || child->m_Visible == false)
        {
            continue;
        }

        const float availableWidth = std::max(0.0f, contentWidth - child->m_Margin.Left - child->m_Margin.Right);
        const float availableHeight = std::max(0.0f, contentHeight - child->m_Margin.Top - child->m_Margin.Bottom);
        math::Vec2 childSize = child->m_DesiredSize;
        math::Vec2 childPosition = child->m_RequestedPosition;

        if (m_LayoutMode == UILayoutMode::Vertical)
        {
            if (child->m_VerticalAlignment == UIAlignment::Stretch)
            {
                childSize.y = child->ClampSize(math::Vec2(childSize.x, verticalStretchHeight)).y;
            }
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

        // Absolute配置でもStretchを明示した子だけは親の確定領域へ追従します。
        // Dock Page内のPanelは前Frameの親サイズに依存せず同FrameでResizeできます。
        if (m_LayoutMode == UILayoutMode::Absolute)
        {
            if (child->m_HorizontalAlignment == UIAlignment::Stretch)
            {
                childSize.x = child->ClampSize(math::Vec2(availableWidth, childSize.y)).x;
                childPosition.x = m_Padding.Left + child->m_Margin.Left;
            }
            if (child->m_VerticalAlignment == UIAlignment::Stretch)
            {
                childSize.y = child->ClampSize(math::Vec2(childSize.x, availableHeight)).y;
                childPosition.y = m_Padding.Top + child->m_Margin.Top;
            }
        }
        child->ArrangeRecursive(childPosition, childSize);
    }
    m_ArrangeDirty = false;
}

void UIElement::BuildDrawListRecursive(UIDrawList& drawList, const math::Vec2& parentAbsolutePosition, const UITransform2D& parentWorldTransform, const UIClipRect& inheritedClip, UIDrawListCacheMetrics* cacheMetrics)
{
    // 描画要求を再構築する直前に消費します。構築中の新たな無効化は次Frameへ残します。
    // 非表示Subtreeは走査しません。再表示はMeasureを無効化して全体を再構築します。
    const bool wasDirty = m_PaintDirty || m_GeometryDirty || m_StyleDirty;
    m_PaintDirty = false;
    m_GeometryDirty = false;
    m_StyleDirty = false;
    if (m_Visible == false)
    {
        return;
    }

    const math::Vec2 absolutePosition(parentAbsolutePosition.x + m_Position.x, parentAbsolutePosition.y + m_Position.y);
    const math::Vec2 pivot(absolutePosition.x + m_Size.x * m_TransformPivot.x, absolutePosition.y + m_Size.y * m_TransformPivot.y);
    const UITransform2D localTransform = UITransform2D::CreateScaleRotation(pivot, m_Rotation, m_Scale);
    const UITransform2D worldTransform = UITransform2D::Combine(parentWorldTransform, localTransform);

    auto* cache = m_DrawListCache.get();
    bool eligible = false;
    const math::Vec4 visualColor = cache != nullptr
        ? ApplyVisualColor(math::Vec4(1.0f, 1.0f, 1.0f, 1.0f)) : math::Vec4{};
    if (cache != nullptr)
    {
        if (cache->EligibilityKnown == false)
        {
            cache->Eligible = IsSubtreeDrawListCacheSupported();
            cache->EligibilityKnown = true;
        }
        eligible = cache->Eligible;
        const auto& savedTransform = cache->WorldTransform;
        const bool sameTransform = savedTransform.M00 == worldTransform.M00 &&
            savedTransform.M01 == worldTransform.M01 && savedTransform.M10 == worldTransform.M10 &&
            savedTransform.M11 == worldTransform.M11 && savedTransform.Translation == worldTransform.Translation;
        const bool sameClip = cache->InheritedClip.Enabled == inheritedClip.Enabled &&
            (inheritedClip.Enabled == false ||
                (cache->InheritedClip.Rect.Min == inheritedClip.Rect.Min && cache->InheritedClip.Rect.Max == inheritedClip.Rect.Max));
        // 最終座標のSnapshotなので、親条件が変わった際に再baseせず通常経路へ戻します。
        if (eligible == true && cache->Valid == true && wasDirty == false &&
            cache->AbsolutePosition == absolutePosition && cache->Size == m_Size &&
            sameTransform == true && sameClip == true && cache->VisualColor == visualColor)
        {
            drawList.AppendRange(cache->Commands);
            if (cacheMetrics != nullptr)
            {
                ++cacheMetrics->HitCount;
                cacheMetrics->ReplayedCommandCount += cache->Commands.GetCommandCount();
            }
            return;
        }
        cache->Valid = false;
        if (cacheMetrics != nullptr)
        {
            if (eligible == true)
            {
                ++cacheMetrics->MissCount;
            }
            else
            {
                ++cacheMetrics->RejectedCount;
            }
        }
        // 失効時に古いCommandの参照を解放し、capacityだけを再利用します。
        cache->Commands.Clear();
    }

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
            child->BuildDrawListRecursive(drawList, absolutePosition, worldTransform, childClip, cacheMetrics);
        }
    }
    // Widget構築中のCache設定変更に備え、所有状態を再取得します。
    cache = m_DrawListCache.get();
    // 毎Frame変わるSubtreeでは保持用コピーを行いません。clean Frameを一度通常生成してから採取します。
    // OnBuild中に再無効化された場合もSnapshotを確定しません。
    if (cache != nullptr && eligible == true && wasDirty == false &&
        m_PaintDirty == false && m_GeometryDirty == false && m_StyleDirty == false)
    {
        const auto& commands = drawList.GetCommands();
        const bool hasResourceCommand = std::any_of(commands.begin() + firstCommand, commands.end(),
            [](const UIDrawCommand& command)
            {
                return command.Type == UIDrawCommandType::Image || command.Texture != nullptr ||
                    command.TextureView.GetSourceType() != UITextureSourceType::None;
            });
        if (hasResourceCommand == false)
        {
            cache->Commands.AppendRange(drawList, firstCommand);
            cache->AbsolutePosition = absolutePosition;
            cache->Size = m_Size;
            cache->WorldTransform = worldTransform;
            cache->InheritedClip = inheritedClip;
            cache->VisualColor = visualColor;
            cache->Valid = true;
            if (cacheMetrics != nullptr)
            {
                ++cacheMetrics->CaptureCount;
            }
        }
        else if (cacheMetrics != nullptr)
        {
            // Font/Textureの内容世代をまだ追跡しないため、Ref寿命だけを根拠に再利用しません。
            ++cacheMetrics->RejectedCount;
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
    if (previous != context)
    {
        InvalidateVisual(true, true, false);
    }
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
    for (UIElement* current = this; current != nullptr; current = current->m_Parent)
    {
        if (current->m_DrawListCache != nullptr)
        {
            current->m_DrawListCache->EligibilityKnown = false;
            current->m_DrawListCache->Valid = false;
        }
    }
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
