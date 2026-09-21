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
        m_Input->SetOnSubmit([this]() { Commit(); });
        m_Input->SetOnFocusLost([this]() { Commit(); });
        m_Input->SetOnStep([this](bool increase)
            {
                if (increase == true)
                {
                    Increment();
                }
                else
                {
                    Decrement();
                }
            });
        m_Input->SetInputFilter([](const std::string& text)
            {
                return IsNumericPrefix(text);
            });
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

    void SetStep(double step)
    {
        if (std::isfinite(step) == true && step > 0.0)
        {
            m_Step = step;
        }
    }

    double GetStep() const { return m_Step; }

    // 編集途中の文字列は保持し、Enter/Focus Lost等の確定境界から呼び出します。
    // 不正・空入力は直前の確定値に戻し、範囲外はClampした値を表示します。
    void Commit()
    {
        double parsed = 0.0;
        if (TryParse(m_Input->GetText(), parsed) == true)
        {
            const double next = std::clamp(parsed, m_Min, m_Max);
            if (next != m_Value)
            {
                m_Value = next;
                if (m_OnValueChanged != nullptr)
                {
                    m_OnValueChanged(m_Value);
                }
            }
        }
        SetValue(m_Value);
    }

    void Increment() { StepBy(1.0); }
    void Decrement() { StepBy(-1.0); }


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
    void StepBy(double direction)
    {
        const double next = m_Value + direction * m_Step;
        if (std::isfinite(next) == false)
        {
            return;
        }
        const double clamped = std::clamp(next, m_Min, m_Max);
        if (clamped == m_Value)
        {
            Commit();
            return;
        }
        m_Value = clamped;
        SetValue(m_Value);
        if (m_OnValueChanged != nullptr)
        {
            m_OnValueChanged(m_Value);
        }
    }

    static bool IsNumericPrefix(const std::string& text)
    {
        // 十進表記と指数表記の途中状態を許可します（例: "-", ".", "1e-"）。
        // UTF-8の非ASCII文字や数値以外の貼り付けは確定前に拒否します。
        std::size_t index = 0u;
        if (index < text.size() && (text[index] == '-' || text[index] == '+'))
        {
            ++index;
        }
        bool mantissaDigit = false;
        while (index < text.size() && text[index] >= '0' && text[index] <= '9')
        {
            mantissaDigit = true;
            ++index;
        }
        if (index < text.size() && text[index] == '.')
        {
            ++index;
            while (index < text.size() && text[index] >= '0' && text[index] <= '9')
            {
                mantissaDigit = true;
                ++index;
            }
        }
        if (index < text.size() && (text[index] == 'e' || text[index] == 'E'))
        {
            if (mantissaDigit == false)
            {
                return false;
            }
            ++index;
            if (index < text.size() && (text[index] == '-' || text[index] == '+'))
            {
                ++index;
            }
            while (index < text.size() && text[index] >= '0' && text[index] <= '9')
            {
                ++index;
            }
        }
        return index == text.size();
    }

    static bool TryParse(const std::string& text, double& value)
    {
        if (text.empty())
        {
            return false;
        }
        const char* begin = text.data();
        const char* end = begin + text.size();
        if (*begin == '+')
        {
            ++begin;
            if (begin == end)
            {
                return false;
            }
        }
        const auto result = std::from_chars(begin, end, value, std::chars_format::general);
        // 途中入力と末尾の余分な文字は確定数値として採用しません。
        return result.ec == std::errc{} && result.ptr == end &&
            std::isfinite(value) == true;
    }

    UIInputText* m_Input = nullptr; // 所有権はUIElementのChild Treeにあります。
    ValueChangedHandler m_OnValueChanged;
    double m_Value = 0.0;
    double m_Step = 1.0;
    double m_Min = -1.0e100;
    double m_Max = 1.0e100;
};

} // namespace Raven
