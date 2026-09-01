#pragma once

#include "Raven/Core/Base.h"
#include "Raven/Math/MathVector.h"
#include "Raven/UI/Core/UIDrawList.h"
#include "Raven/UI/Core/UIEvent.h"

#include <limits>
#include <string>
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

    // ChildをTreeから切り離して所有権を呼び出し側へ返します。
    // Capture / Hover / Pressed対象を含むSubtreeでは、破棄・再接続より前にUIContextへ削除境界を通知します。
    Scope<UIElement> DetachChild(UIElement* child);

    // Childを個別に削除します。DetachChild()で返されたScopeをその場で破棄する簡易APIです。
    bool RemoveChild(UIElement* child);
    void ClearChildren();

    // Animation / Serialization / EditorからElementを安定して参照するための論理名です。
    // 同一Parent配下での一意性はPath解決時に検証し、空文字は未命名Elementとして扱います。
    void SetName(std::string name);
    const std::string& GetName() const;

    // '/' 区切りの相対PathからDescendantを検索します。
    // 空Pathは現在Element自身を返します。毎frame利用する用途ではなく、Animation Binding初期解決用です。
    UIElement* FindByPath(const std::string& path);
    const UIElement* FindByPath(const std::string& path) const;

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
    void BuildDrawListRecursive(UIDrawList& drawList, const math::Vec2& parentAbsolutePosition) const;
    void SetContextRecursive(UIContext* context);

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
    bool m_Visible = true;
    bool m_Hovered = false;
    bool m_Pressed = false;
    bool m_MeasureDirty = true;
    bool m_ArrangeDirty = true;
};

} // namespace Raven
