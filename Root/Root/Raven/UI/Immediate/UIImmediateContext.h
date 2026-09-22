#pragma once

#include "Raven/UI/Core/UIContext.h"
#include "Raven/UI/Widgets/UIButton.h"
#include "Raven/UI/Widgets/UILabel.h"
#include "Raven/UI/Widgets/UIInputText.h"
#include "Raven/UI/Widgets/UIPanel.h"
#include "Raven/UI/Widgets/UISlider.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <type_traits>
#include <typeindex>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Raven
{

// Retained Treeを保持したまま、毎Frameの宣言からWidgetを再利用する入口です。
// UIContext::BeginFrameより前（Frame非Active）にBeginFrame/EndFrameを呼び出してください。
// このContextが生成したElementの所有権はUIElement Treeにあり、外部から削除・移譲しない契約です。
class UIImmediateContext
{
public:
    explicit UIImmediateContext(UIContext& context)
        : m_Context(context)
    {
    }

    UIImmediateContext(const UIImmediateContext&) = delete;
    UIImmediateContext& operator=(const UIImmediateContext&) = delete;

    // falseを返した場合、前FrameのTreeとCacheは変更しません。
    bool BeginFrame()
    {
        if (m_FrameActive == true || m_Context.IsFrameActive() == true)
        {
            return false;
        }

        m_FrameActive = true;
        m_Used.clear();
        m_IDStack.clear();
        m_IDKinds.clear();
        m_ParentStack.clear();
        m_ParentStack.push_back(&m_Context.GetRootElement());
        return true;
    }

    // Scopeの不均衡時は破棄せず、次Frameで再構築できるよう失敗を返します。
    bool EndFrame()
    {
        if (m_FrameActive == false || m_ParentStack.size() != 1u ||
            m_IDStack.empty() == false || m_Context.IsFrameActive() == true)
        {
            return false;
        }

        // 深いElementから消すことで親破棄後に子のraw pointerを参照しません。
        // DetachChild経由でFocus / Capture / IMEを既存UIContextへ通知します。
        std::vector<std::pair<std::size_t, std::string>> stale;
        for (const auto& item : m_Widgets)
        {
            if (m_Used.find(item.first) == m_Used.end())
            {
                stale.emplace_back(item.second.Depth, item.first);
            }
        }
        std::sort(stale.begin(), stale.end(),
            [](const auto& a, const auto& b) { return a.first > b.first; });
        for (const auto& item : stale)
        {
            const auto found = m_Widgets.find(item.second);
            if (found == m_Widgets.end())
            {
                continue;
            }
            UIElement* element = found->second.Element;
            UIElement* parent = element->GetParent();
            if (parent != nullptr)
            {
                parent->RemoveChild(element);
            }
            m_ButtonStates.erase(item.second);
            m_SliderStates.erase(item.second);
            m_TextStates.erase(item.second);
            m_Widgets.erase(found);
        }

        m_ParentStack.clear();
        m_IDStack.clear();
        m_IDKinds.clear();
        m_Used.clear();
        m_FrameActive = false;
        return true;
    }

    // 宣言側でBegin/Endの対応を崩した場合に限り、既存Widgetを破棄せずFrameを中断します。
    void AbortFrame()
    {
        m_ParentStack.clear();
        m_IDStack.clear();
        m_IDKinds.clear();
        m_Used.clear();
        m_FrameActive = false;
    }

    // Immediate API: IDは表示文字列と独立して指定します。
    // UIContext側の描画Frameより前に宣言し、入力結果は次回の宣言で受け取ります。
    UILabel* Text(const std::string& id, const std::string& text,
        const Ref<UIFontAtlas>& font = nullptr)
    {
        UILabel* label = GetOrCreate<UILabel>(id);
        if (label != nullptr)
        {
            label->SetText(text);
            if (font != nullptr)
            {
                label->SetFont(font);
            }
        }
        return label;
    }

    // Click callbackはWidget生成時だけ登録し、入力Frame中はContext本体を触りません。
    // Pendingは次に同じIDが宣言された時だけ消費します。
    bool Button(const std::string& id, const math::Vec2& size = math::Vec2(120.0f, 28.0f))
    {
        const std::string key = MakeKey(id);
        UIButton* button = GetOrCreate<UIButton>(id);
        if (button == nullptr)
        {
            return false;
        }
        button->SetPreferredSize(size);
        auto& state = m_ButtonStates[key];
        if (state == nullptr)
        {
            state = std::make_shared<ButtonState>();
            const std::weak_ptr<ButtonState> weak = state;
            button->SetOnClick([weak]()
            {
                if (const auto current = weak.lock())
                {
                    current->Clicked = true;
                }
            });
        }
        const bool clicked = state->Clicked;
        state->Clicked = false;
        return clicked;
    }

    // 表示ラベルはButtonの子として保持します。Hit Testを無効化して
    // Mouse Targetが子Labelに移り、親ButtonのClick判定が失敗するのを防ぎます。
    bool Button(const std::string& id, const std::string& caption,
        const Ref<UIFontAtlas>& font,
        const math::Vec2& size = math::Vec2(120.0f, 28.0f))
    {
        // 同一Frameでの二重宣言は既存Widgetのラベルも書き換えません。
        if (m_FrameActive == false || id.empty() ||
            m_Used.find(MakeKey(id)) != m_Used.end())
        {
            return false;
        }
        const bool clicked = Button(id, size);
        const auto found = m_Widgets.find(MakeKey(id));
        if (found == m_Widgets.end() || m_Used.find(found->first) == m_Used.end() ||
            found->second.Type != std::type_index(typeid(UIButton)))
        {
            return false;
        }

        UIButton* button = static_cast<UIButton*>(found->second.Element);
        UILabel* label = nullptr;
        if (button->GetChildren().empty() == true)
        {
            auto child = CreateScope<UILabel>();
            child->SetHitTestVisible(false);
            label = static_cast<UILabel*>(button->AddChild(std::move(child)));
        }
        else
        {
            label = dynamic_cast<UILabel*>(button->GetChildren().front().get());
        }
        if (label != nullptr)
        {
            label->SetText(caption);
            if (font != nullptr)
            {
                label->SetFont(font);
            }
            label->SetPosition(math::Vec2(8.0f, 4.0f));
            label->SetPreferredSize(math::Vec2(
                std::max(0.0f, size.x - 16.0f), std::max(0.0f, size.y - 8.0f)));
        }
        return clicked;
    }

    // PanelのTreeとID Scopeをまとめて開きます。EndContainer()で閉じてください。
    UIPanel* BeginPanel(const std::string& id, const math::Vec2& position,
        const math::Vec2& size, float padding = 8.0f)
    {
        UIPanel* panel = BeginContainer<UIPanel>(id);
        if (panel != nullptr)
        {
            panel->SetPosition(position);
            panel->SetPreferredSize(size);
            panel->SetLayoutMode(UILayoutMode::Vertical);
            panel->SetPadding(std::max(0.0f, padding));
        }
        return panel;
    }

    // valueは呼び出し側の所有物です。CallbackにそのPointerを保存せず、
    // UI入力で生じた値だけをCacheし、次回の宣言時にvalueへ反映します。
    bool SliderFloat(const std::string& id, float* value, float minimum, float maximum,
        const math::Vec2& size = math::Vec2(160.0f, 24.0f))
    {
        if (value == nullptr)
        {
            return false;
        }
        const std::string key = MakeKey(id);
        UISlider* slider = GetOrCreate<UISlider>(id);
        if (slider == nullptr)
        {
            return false;
        }
        auto& state = m_SliderStates[key];
        if (state == nullptr)
        {
            state = std::make_shared<SliderState>();
            const std::weak_ptr<SliderState> weak = state;
            slider->SetOnValueChanged([weak](float next)
            {
                if (const auto current = weak.lock())
                {
                    if (current->Synchronizing == false)
                    {
                        current->Value = next;
                        current->Changed = true;
                    }
                }
            });
        }
        slider->SetPreferredSize(size);
        // SetRange / SetValueも通知を出すため、外部同期とユーザー操作を分離します。
        state->Synchronizing = true;
        slider->SetRange(minimum, maximum);
        const bool changed = state->Changed;
        if (changed == true)
        {
            *value = state->Value;
            state->Changed = false;
        }
        slider->SetValue(*value);
        *value = slider->GetValue();
        state->Synchronizing = false;
        return changed;
    }

    // UIInputTextの編集通知は一時的なstd::string*へ直接書き戻しません。
    // 次回宣言時に変更を反映し、外部値の更新時のみWidgetへ同期します。
    bool InputText(const std::string& id, std::string* value,
        const Ref<UIFontAtlas>& font = nullptr,
        const math::Vec2& size = math::Vec2(180.0f, 30.0f))
    {
        if (value == nullptr)
        {
            return false;
        }
        const std::string key = MakeKey(id);
        UIInputText* input = GetOrCreate<UIInputText>(id);
        if (input == nullptr)
        {
            return false;
        }
        auto& state = m_TextStates[key];
        if (state == nullptr)
        {
            state = std::make_shared<TextState>();
            if (font != nullptr)
            {
                input->SetFont(font);
            }
            const std::weak_ptr<TextState> weak = state;
            input->SetOnChange([weak](const std::string& next)
            {
                if (const auto current = weak.lock())
                {
                    if (current->Synchronizing == false)
                    {
                        current->Value = next;
                        current->Changed = true;
                    }
                }
            });
        }
        input->SetPreferredSize(size);
        const bool changed = state->Changed;
        if (changed == true)
        {
            *value = state->Value;
            state->Changed = false;
        }
        if (input->GetText() != *value)
        {
            state->Synchronizing = true;
            input->SetText(*value);
            state->Synchronizing = false;
        }
        return changed;
    }

    // ID Stackは長さ付きで符号化し、例えば ("ab","c") と ("a","bc") を区別します。
    bool PushID(const std::string& id)
    {
        if (m_FrameActive == false || id.empty())
        {
            return false;
        }
        m_IDStack.push_back(id);
        m_IDKinds.push_back(false);
        return true;
    }

    bool PopID()
    {
        if (m_FrameActive == false || m_IDStack.empty() || m_IDKinds.back() == true)
        {
            return false;
        }
        m_IDStack.pop_back();
        m_IDKinds.pop_back();
        return true;
    }

    // 同じIDを同一Frameで2回宣言した場合はnullptrを返します。
    // 型または親が変わった場合も暗黙にWidgetを交換せず、明示的なID変更を要求します。
    template<typename T, typename... Args>
    T* GetOrCreate(const std::string& id, Args&&... args)
    {
        static_assert(std::is_base_of<UIElement, T>::value, "T must derive from UIElement");
        if (m_FrameActive == false || id.empty() || m_Context.IsFrameActive() == true)
        {
            return nullptr;
        }

        const std::string key = MakeKey(id);
        if (m_Used.find(key) != m_Used.end())
        {
            return nullptr;
        }

        UIElement* parent = m_ParentStack.back();
        const auto found = m_Widgets.find(key);
        if (found != m_Widgets.end())
        {
            if (found->second.Type != std::type_index(typeid(T)) ||
                found->second.Parent != parent)
            {
                return nullptr;
            }
            m_Used.insert(key);
            return static_cast<T*>(found->second.Element);
        }

        auto widget = CreateScope<T>(std::forward<Args>(args)...);
        T* raw = widget.get();
        UIElement* added = parent == &m_Context.GetRootElement()
            ? m_Context.AddRootChild(std::move(widget))
            : parent->AddChild(std::move(widget));
        if (added == nullptr)
        {
            return nullptr;
        }
        m_Widgets.emplace(key, Entry{ raw, parent, std::type_index(typeid(T)), m_ParentStack.size() });
        m_Used.insert(key);
        return raw;
    }

    // BeginContainer/EndContainerはWidgetの親階層とID階層を同時に更新します。
    template<typename T, typename... Args>
    T* BeginContainer(const std::string& id, Args&&... args)
    {
        T* container = GetOrCreate<T>(id, std::forward<Args>(args)...);
        if (container != nullptr)
        {
            m_ParentStack.push_back(container);
            m_IDStack.push_back(id);
            m_IDKinds.push_back(true);
        }
        return container;
    }

    bool EndContainer()
    {
        if (m_FrameActive == false || m_ParentStack.size() <= 1u ||
            m_IDStack.empty() || m_IDKinds.back() == false)
        {
            return false;
        }
        m_ParentStack.pop_back();
        m_IDStack.pop_back();
        m_IDKinds.pop_back();
        return true;
    }

    bool IsFrameActive() const { return m_FrameActive; }
    std::size_t GetCachedWidgetCount() const { return m_Widgets.size(); }

private:
    struct ButtonState { bool Clicked = false; };
    struct SliderState
    {
        float Value = 0.0f;
        bool Changed = false;
        bool Synchronizing = false;
    };

    struct TextState
    {
        std::string Value;
        bool Changed = false;
        bool Synchronizing = false;
    };

    struct Entry
    {
        UIElement* Element;
        UIElement* Parent;
        std::type_index Type;
        std::size_t Depth;
    };

    static void AppendSegment(std::string& key, const std::string& segment)
    {
        key += std::to_string(segment.size());
        key += ':';
        key += segment;
    }

    std::string MakeKey(const std::string& id) const
    {
        std::string key;
        for (const auto& segment : m_IDStack)
        {
            AppendSegment(key, segment);
        }
        AppendSegment(key, id);
        return key;
    }

    UIContext& m_Context;
    std::unordered_map<std::string, Entry> m_Widgets;
    std::unordered_set<std::string> m_Used;
    std::unordered_map<std::string, std::shared_ptr<ButtonState>> m_ButtonStates;
    std::unordered_map<std::string, std::shared_ptr<SliderState>> m_SliderStates;
    std::unordered_map<std::string, std::shared_ptr<TextState>> m_TextStates;
    std::vector<std::string> m_IDStack;
    // PushIDとContainerのPop順序を混同させないためのScope種別です。
    std::vector<bool> m_IDKinds;
    std::vector<UIElement*> m_ParentStack;
    bool m_FrameActive = false;
};

} // namespace Raven
