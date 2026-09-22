#pragma once

#include "Raven/UI/Core/UIContext.h"

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
    std::vector<std::string> m_IDStack;
    // PushIDとContainerのPop順序を混同させないためのScope種別です。
    std::vector<bool> m_IDKinds;
    std::vector<UIElement*> m_ParentStack;
    bool m_FrameActive = false;
};

} // namespace Raven
