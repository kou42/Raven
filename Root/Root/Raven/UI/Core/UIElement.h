#pragma once

#include "Raven/Core/Base.h"
#include "Raven/Math/MathVector.h"
#include "Raven/UI/Core/UIDrawList.h"
#include "Raven/UI/Core/UIEvent.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace Raven
{

class UIContext;

enum class UILayoutMode { Absolute = 0, Vertical, Horizontal };
enum class UIAlignment { Start = 0, Center, End, Stretch };

struct UIThickness
{
    float Left = 0.0f;
    float Top = 0.0f;
    float Right = 0.0f;
    float Bottom = 0.0f;

    UIThickness();
    explicit UIThickness(float uniform);
    UIThickness(float horizontal, float vertical);
};

// Raven UIのRetained Mode Treeを構成する基底要素です。
// MeasureはLeafからParentへ必要Sizeを集約し、ArrangeはParentからChildへ実配置を配ります。
// MarginはElement外側、PaddingはContainer内側の余白として明確に分離します。
class UIElement
{
public:
    UIElement();
    virtual ~UIElement();
    UIElement(const UIElement&) = delete;
    UIElement& operator=(const UIElement&) = delete;
    UIElement(UIElement&&) = delete;
    UIElement& operator=(UIElement&&) = delete;

    UIElement* AddChild(Scope<UIElement> child);
    Scope<UIElement> DetachChild(UIElement* child);
    bool RemoveChild(UIElement* child);
    void ClearChildren();

    bool SetName(std::string name);
    const std::string& GetName() const { return m_Name; }

    UIElement* FindByPath(const std::string& path)
    {
        return const_cast<UIElement*>(static_cast<const UIElement*>(this)->FindByPath(path));
    }

    const UIElement* FindByPath(const std::string& path) const
    {
        if (path.empty())
        {
            return this;
        }

        const UIElement* current = this;
        std::size_t begin = 0u;
        while (begin < path.size())
        {
            const std::size_t separator = path.find('/', begin);
            const std::size_t count = (separator == std::string::npos) ? std::string::npos : separator - begin;
            const std::string segment = path.substr(begin, count);
            if (segment.empty())
            {
                return nullptr;
            }

            const UIElement* matched = nullptr;
            for (const auto& child : current->m_Children)
            {
                if (child != nullptr && child->m_Name == segment)
                {
                    if (matched != nullptr)
                    {
                        return nullptr;
                    }
                    matched = child.get();
                }
            }
            if (matched == nullptr)
            {
                return nullptr;
            }

            current = matched;
            if (separator == std::string::npos)
            {
                break;
            }
            begin = separator + 1u;
        }
        return current;
    }

    uint64_t GetTreeGeneration() const;

    void SetPosition(const math::Vec2& value);
    void SetSize(const math::Vec2& value);
    void SetPreferredSize(const math::Vec2& value);
    void SetMinSize(const math::Vec2& value);
    void SetMaxSize(const math::Vec2& value);
    void SetVisible(bool value);
    void SetLayoutMode(UILayoutMode value);
    void SetHorizontalAlignment(UIAlignment value);
    void SetVerticalAlignment(UIAlignment value);
    void SetPadding(const UIThickness& value);
    void SetPadding(float value);
    void SetMargin(const UIThickness& value);
    void SetMargin(float value);
    void SetSpacing(float value);

    // falseにすると自身のMeasureは通常通り実行しつつ、親のDesiredSize集約からだけ除外します。
    // ScrollView ContentやOverlay Decorationのように、子の実サイズと親Viewportサイズを分離したい場合に利用します。
    void SetAffectsParentMeasure(bool value);
    bool GetAffectsParentMeasure() const { return m_AffectsParentMeasure; }

    void SetRotation(float value) { m_Rotation = value; }
    float GetRotation() const { return m_Rotation; }
    void SetScale(const math::Vec2& value) { m_Scale = value; }
    const math::Vec2& GetScale() const { return m_Scale; }
    void SetTransformPivot(const math::Vec2& value) { m_TransformPivot = value; }
    const math::Vec2& GetTransformPivot() const { return m_TransformPivot; }

    bool TryScreenToLocalPosition(const math::Vec2& screenPosition, math::Vec2& outLocalPosition) const
    {
        std::vector<const UIElement*> chain;
        const UIElement* current = this;
        while (current != nullptr)
        {
            chain.push_back(current);
            current = current->m_Parent;
        }
        std::reverse(chain.begin(), chain.end());

        UITransform2D worldTransform = UITransform2D::Identity();
        math::Vec2 absolutePosition(0.0f, 0.0f);
        math::Vec2 thisAbsolutePosition(0.0f, 0.0f);

        for (const UIElement* element : chain)
        {
            absolutePosition.x += element->m_Position.x;
            absolutePosition.y += element->m_Position.y;

            const math::Vec2 pivot(
                absolutePosition.x + element->m_Size.x * element->m_TransformPivot.x,
                absolutePosition.y + element->m_Size.y * element->m_TransformPivot.y);
            const UITransform2D localTransform = UITransform2D::CreateScaleRotation(
                pivot,
                element->m_Rotation,
                element->m_Scale);
            worldTransform = UITransform2D::Combine(worldTransform, localTransform);

            if (element == this)
            {
                thisAbsolutePosition = absolutePosition;
            }
        }

        math::Vec2 layoutPosition;
        if (worldTransform.TryInverseTransformPoint(screenPosition, layoutPosition) == false)
        {
            return false;
        }

        outLocalPosition = math::Vec2(
            layoutPosition.x - thisAbsolutePosition.x,
            layoutPosition.y - thisAbsolutePosition.y);
        return true;
    }

    void SetClipChildren(bool value) { m_ClipChildren = value; }
    bool GetClipChildren() const { return m_ClipChildren; }

    void SetOpacity(float value) { m_Opacity = std::clamp(value, 0.0f, 1.0f); }
    float GetOpacity() const { return m_Opacity; }
    void SetTintColor(const math::Vec4& value) { m_TintColor = value; }
    const math::Vec4& GetTintColor() const { return m_TintColor; }

    math::Vec4 ApplyVisualColor(const math::Vec4& color) const
    {
        math::Vec4 result = color;
        const UIElement* current = this;
        while (current != nullptr)
        {
            result.x *= current->m_TintColor.x;
            result.y *= current->m_TintColor.y;
            result.z *= current->m_TintColor.z;
            result.w *= current->m_TintColor.w * current->m_Opacity;
            current = current->m_Parent;
        }
        return result;
    }

    const math::Vec2& GetPosition() const;
    const math::Vec2& GetSize() const;
    const math::Vec2& GetPreferredSize() const;
    const math::Vec2& GetDesiredSize() const;
    const UIThickness& GetPadding() const;
    const UIThickness& GetMargin() const;
    bool IsVisible() const;
    bool IsHovered() const;
    bool IsPressed() const;
    bool IsMeasureDirty() const;
    bool IsArrangeDirty() const;
    UIElement* GetParent();
    const UIElement* GetParent() const;
    UIContext* GetContext() { return m_Context; }
    const UIContext* GetContext() const { return m_Context; }
    const std::vector<Scope<UIElement>>& GetChildren() const;

    void SetHovered(bool value);
    void SetPressed(bool value);
    void HandleMouseEvent(UIMouseEvent& event);
    void BuildDrawList(UIDrawList& drawList);

protected:
    virtual void OnMouseEvent(UIMouseEvent& event);
    virtual void OnBuildDrawList(UIDrawList& drawList, const math::Vec2& absolutePosition) const;

private:
    friend class UIContext;

    math::Vec2 ClampSize(const math::Vec2& size) const;
    math::Vec2 ResolveRootSize() const;
    math::Vec2 GetDesiredSizeWithMargin() const;
    void InvalidateMeasure();
    void InvalidateArrange();
    void MeasureRecursive();
    static float ResolveAlignedOffset(float available, float size, UIAlignment alignment);
    void ArrangeRecursive(const math::Vec2& position, const math::Vec2& arrangedSize);
    void BuildDrawListRecursive(
        UIDrawList& drawList,
        const math::Vec2& parentAbsolutePosition,
        const UITransform2D& parentWorldTransform,
        const UIClipRect& inheritedClip) const;
    void SetContextRecursive(UIContext* context);
    void NotifyBindingTreeChanged();
    UIElement* GetTreeRoot();
    const UIElement* GetTreeRoot() const;

private:
    std::string m_Name;
    math::Vec2 m_Position{};
    math::Vec2 m_Size{};
    math::Vec2 m_PreferredSize{};
    math::Vec2 m_DesiredSize{};
    math::Vec2 m_MinSize{};
    math::Vec2 m_MaxSize{ std::numeric_limits<float>::max(), std::numeric_limits<float>::max() };
    UIElement* m_Parent = nullptr;
    UIContext* m_Context = nullptr;
    std::vector<Scope<UIElement>> m_Children;
    UIThickness m_Padding{};
    UIThickness m_Margin{};
    UILayoutMode m_LayoutMode = UILayoutMode::Absolute;
    UIAlignment m_HorizontalAlignment = UIAlignment::Start;
    UIAlignment m_VerticalAlignment = UIAlignment::Start;
    float m_Spacing = 0.0f;
    float m_Rotation = 0.0f;
    math::Vec2 m_Scale{ 1.0f, 1.0f };
    math::Vec2 m_TransformPivot{ 0.5f, 0.5f };
    float m_Opacity = 1.0f;
    math::Vec4 m_TintColor{ 1.0f, 1.0f, 1.0f, 1.0f };
    uint64_t m_TreeGeneration = 1u;
    bool m_Visible = true;
    bool m_Hovered = false;
    bool m_Pressed = false;
    bool m_MeasureDirty = true;
    bool m_ArrangeDirty = true;
    bool m_ClipChildren = false;
    bool m_AffectsParentMeasure = true;
};

} // namespace Raven
