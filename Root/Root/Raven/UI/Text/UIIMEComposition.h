#pragma once

#include "Raven/UI/Text/UIUtf8.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace Raven
{

// IME変換中の表示専用状態です。UITextEditBufferやUndo/Redo履歴を変更しません。
// 全indexはUTF-8 byte offsetではなくUnicode codepoint境界です。
// Platform固有のUTF-16 offsetはAdapter側で変換してから渡します。
class UIIMEComposition
{
public:
    void Begin(std::size_t replacementStart, std::size_t replacementEnd)
    {
        m_Active = true;
        m_Text.clear();
        m_ReplacementStart = std::min(replacementStart, replacementEnd);
        m_ReplacementEnd = std::max(replacementStart, replacementEnd);
        m_Cursor = 0u;
        m_SelectionStart = 0u;
        m_SelectionEnd = 0u;
    }

    // 変換候補の更新は履歴に触れず、この一時文字列だけを差し替えます。
    // 不正なUTF-8はUIUtf8の置換文字へ正規化し、indexの範囲を保ちます。
    void Update(std::string_view text, std::size_t cursor,
        std::size_t selectionStart, std::size_t selectionEnd)
    {
        if (m_Active == false)
        {
            return;
        }

        m_Text.clear();
        std::size_t offset = 0u;
        std::uint32_t codepoint = 0u;
        std::size_t length = 0u;
        while (UIUtf8::DecodeNext(text, offset, codepoint))
        {
            AppendCodepoint(m_Text, codepoint);
            ++length;
        }

        m_Cursor = std::min(cursor, length);
        m_SelectionStart = std::min(selectionStart, length);
        m_SelectionEnd = std::min(selectionEnd, length);
        if (m_SelectionStart > m_SelectionEnd)
        {
            std::swap(m_SelectionStart, m_SelectionEnd);
        }
    }

    // Commit結果は呼び出し側が取得してから既存の編集APIで一度だけ反映します。
    // End自体は確定文字列を挿入せず、Cancelと同様に一時状態のみ消去します。
    void End()
    {
        m_Active = false;
        m_Text.clear();
        m_ReplacementStart = 0u;
        m_ReplacementEnd = 0u;
        m_Cursor = 0u;
        m_SelectionStart = 0u;
        m_SelectionEnd = 0u;
    }

    void Cancel() { End(); }

    bool IsActive() const { return m_Active; }
    const std::string& GetText() const { return m_Text; }
    std::size_t GetReplacementStart() const { return m_ReplacementStart; }
    std::size_t GetReplacementEnd() const { return m_ReplacementEnd; }
    std::size_t GetCursor() const { return m_Cursor; }
    std::pair<std::size_t, std::size_t> GetSelection() const
    {
        return { m_SelectionStart, m_SelectionEnd };
    }

private:
    static void AppendCodepoint(std::string& output, std::uint32_t codepoint)
    {
        if (codepoint <= 0x7Fu)
        {
            output.push_back(static_cast<char>(codepoint));
        }
        else if (codepoint <= 0x7FFu)
        {
            output.push_back(static_cast<char>(0xC0u | (codepoint >> 6u)));
            output.push_back(static_cast<char>(0x80u | (codepoint & 0x3Fu)));
        }
        else if (codepoint <= 0xFFFFu)
        {
            output.push_back(static_cast<char>(0xE0u | (codepoint >> 12u)));
            output.push_back(static_cast<char>(0x80u | ((codepoint >> 6u) & 0x3Fu)));
            output.push_back(static_cast<char>(0x80u | (codepoint & 0x3Fu)));
        }
        else
        {
            output.push_back(static_cast<char>(0xF0u | (codepoint >> 18u)));
            output.push_back(static_cast<char>(0x80u | ((codepoint >> 12u) & 0x3Fu)));
            output.push_back(static_cast<char>(0x80u | (codepoint & 0x3Fu)));
        }
    }

    bool m_Active = false;
    std::string m_Text;
    std::size_t m_ReplacementStart = 0u;
    std::size_t m_ReplacementEnd = 0u;
    std::size_t m_Cursor = 0u;
    std::size_t m_SelectionStart = 0u;
    std::size_t m_SelectionEnd = 0u;
};

} // namespace Raven
