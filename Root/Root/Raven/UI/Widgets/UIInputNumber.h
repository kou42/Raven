#pragma once

#include "Raven/UI/Widgets/UIInputText.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <functional>
#include <string>
#include <system_error>
#include <utility>

namespace Raven
{

// 数値の編集途中（空文字・"-"・"1."など）はTextとして保持し、
// 完全な有限数に変換できたときだけValueと通知を更新します。
class UIInputNumber final : public UIElement
{
public:
    using ValueChangedHandler = std::function<void(double)>;

    UIInputNumber()
    {
        auto input = CreateScope<UIInputText>();
        input->SetPosition(math::Vec2(0.0f, 0.0f));
        input->SetSize(math::Vec2(180.0f, 30.0f));
        m_Input = input.get();
        m_Input->SetOnChange([this](const std::string& text)
            {
                double parsed = 0.0;
                if (TryParse(text, parsed) == false)
                {
                    return;
                }
                const double next = std::clamp(parsed, m_Min, m_Max);
                if (next == m_Value)
                {
                    return;
                }
                m_Value = next;
                if (m_OnValueChanged != nullptr)
                {
                    m_OnValueChanged(m_Value);
                }
            });
        AddChild(std::move(input));
        SetValue(0.0);
    }

    void SetFont(const Ref<UIFontAtlas>& font) { m_Input->SetFont(font); }
    void SetClipboard(UIInputText::ClipboardReader reader, UIInputText::ClipboardWriter writer)
    {
        m_Input->SetClipboard(std::move(reader), std::move(writer));
    }
    void SetOnValueChanged(ValueChangedHandler handler) { m_OnValueChanged = std::move(handler); }

    void SetRange(double minimum, double maximum)
    {
        if (std::isfinite(minimum) == false || std::isfinite(maximum) == false ||
            minimum > maximum)
        {
            return;
        }
        m_Min = minimum;
        m_Max = maximum;
        SetValue(m_Value);
    }

    void SetValue(double value)
    {
        if (std::isfinite(value) == false)
        {
            return;
        }
        m_Value = std::clamp(value, m_Min, m_Max);
        char buffer[64]{};
        const auto result = std::to_chars(buffer, buffer + sizeof(buffer),
            m_Value, std::chars_format::general);
        if (result.ec == std::errc{})
        {
            // 外部からの値設定は新しい編集セッションとし、Undo履歴をリセットします。
            m_Input->SetText(std::string_view(buffer, static_cast<std::size_t>(result.ptr - buffer)));
        }
    }

    double GetValue() const { return m_Value; }
    const std::string& GetEditText() const { return m_Input->GetText(); }
    UIInputText& GetInputText() { return *m_Input; }

private:
    static bool TryParse(const std::string& text, double& value)
    {
        if (text.empty())
        {
            return false;
        }
        const char* begin = text.data();
        const char* end = begin + text.size();
        const auto result = std::from_chars(begin, end, value, std::chars_format::general);
        // 途中入力と末尾の余分な文字は確定数値として採用しません。
        return result.ec == std::errc{} && result.ptr == end &&
            std::isfinite(value) == true;
    }

    UIInputText* m_Input = nullptr; // 所有権はUIElementのChild Treeにあります。
    ValueChangedHandler m_OnValueChanged;
    double m_Value = 0.0;
    double m_Min = -1.0e100;
    double m_Max = 1.0e100;
};

} // namespace Raven
