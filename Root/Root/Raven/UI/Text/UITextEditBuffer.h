#pragma once

#include "Raven/UI/Text/UIUtf8.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Raven
{

// InputText / InputNumberが共有する、描画やPlatform入力に依存しない編集状態です。
// CursorとAnchorはbyte offsetではなくUnicode codepoint境界のindexです。
// UTF-8の途中を削除・分割しないため、文字列の変更は必ずこのクラスを通します。
// 結合文字・絵文字シーケンス単位の移動は後続のgrapheme対応で拡張します。
class UITextEditBuffer
{
public:
    void SetText(std::string_view text)
    {
        m_Text.clear();
        std::size_t offset = 0u;
        std::uint32_t codepoint = 0u;
        while (UIUtf8::DecodeNext(text, offset, codepoint))
        {
            AppendCodepoint(m_Text, codepoint);
        }
        m_Cursor = GetLength();
        m_Anchor = m_Cursor;
    }

    const std::string& GetText() const { return m_Text; }
    std::size_t GetCursor() const { return m_Cursor; }
    std::size_t GetAnchor() const { return m_Anchor; }
    bool HasSelection() const { return m_Cursor != m_Anchor; }

    std::pair<std::size_t, std::size_t> GetSelection() const
    {
        return std::minmax(m_Cursor, m_Anchor);
    }

    std::size_t GetLength() const
    {
        std::size_t count = 0u;
        std::size_t offset = 0u;
        std::uint32_t codepoint = 0u;
        while (UIUtf8::DecodeNext(m_Text, offset, codepoint))
        {
            ++count;
        }
        return count;
    }

    void MoveCursor(std::size_t index, bool extendSelection = false)
    {
        m_Cursor = std::min(index, GetLength());
        if (extendSelection == false)
        {
            m_Anchor = m_Cursor;
        }
    }

    void SelectAll()
    {
        m_Anchor = 0u;
        m_Cursor = GetLength();
    }

    // 文字入力イベントのUnicode scalar valueをUTF-8へ変換して挿入します。
    // 制御文字と不正なscalarは編集バッファに追加しません（改行は後続の複数行対応）。
    bool InsertCodepoint(std::uint32_t codepoint)
    {
        if (codepoint < 0x20u || codepoint == 0x7Fu ||
            codepoint > 0x10FFFFu ||
            (codepoint >= 0xD800u && codepoint <= 0xDFFFu))
        {
            return false;
        }
        std::string encoded;
        AppendCodepoint(encoded, codepoint);
        return InsertText(encoded);
    }

    bool InsertText(std::string_view text)
    {
        std::string normalized;
        std::size_t offset = 0u;
        std::uint32_t codepoint = 0u;
        while (UIUtf8::DecodeNext(text, offset, codepoint))
        {
            if (codepoint >= 0x20u && codepoint != 0x7Fu)
            {
                AppendCodepoint(normalized, codepoint);
            }
        }
        if (normalized.empty())
        {
            return false;
        }

        const auto selection = GetSelection();
        const std::size_t begin = ByteOffset(selection.first);
        const std::size_t end = ByteOffset(selection.second);
        m_Text.replace(begin, end - begin, normalized);
        m_Cursor = selection.first + CountCodepoints(normalized);
        m_Anchor = m_Cursor;
        return true;
    }

    bool Backspace()
    {
        if (HasSelection() == true)
        {
            return DeleteSelection();
        }
        if (m_Cursor == 0u)
        {
            return false;
        }
        const std::size_t end = ByteOffset(m_Cursor);
        const std::size_t begin = ByteOffset(m_Cursor - 1u);
        m_Text.erase(begin, end - begin);
        --m_Cursor;
        m_Anchor = m_Cursor;
        return true;
    }

    bool DeleteForward()
    {
        if (HasSelection() == true)
        {
            return DeleteSelection();
        }
        if (m_Cursor >= GetLength())
        {
            return false;
        }
        const std::size_t begin = ByteOffset(m_Cursor);
        const std::size_t end = ByteOffset(m_Cursor + 1u);
        m_Text.erase(begin, end - begin);
        return true;
    }

    bool DeleteSelection()
    {
        if (HasSelection() == false)
        {
            return false;
        }
        const auto selection = GetSelection();
        const std::size_t begin = ByteOffset(selection.first);
        const std::size_t end = ByteOffset(selection.second);
        m_Text.erase(begin, end - begin);
        m_Cursor = selection.first;
        m_Anchor = m_Cursor;
        return true;
    }

private:
    static std::size_t CountCodepoints(std::string_view text)
    {
        std::size_t count = 0u;
        std::size_t offset = 0u;
        std::uint32_t codepoint = 0u;
        while (UIUtf8::DecodeNext(text, offset, codepoint))
        {
            ++count;
        }
        return count;
    }

    std::size_t ByteOffset(std::size_t index) const
    {
        std::size_t offset = 0u;
        std::size_t count = 0u;
        std::uint32_t codepoint = 0u;
        while (count < index && UIUtf8::DecodeNext(m_Text, offset, codepoint))
        {
            ++count;
        }
        return offset;
    }

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
            output.push_back(static_cast<char>(0x80u | ((codepoint >> 6u) & 0x3Fu)));
            output.push_back(static_cast<char>(0x80u | (codepoint & 0x3Fu)));
        }
    }

    std::string m_Text;
    std::size_t m_Cursor = 0u;
    std::size_t m_Anchor = 0u;
};

} // namespace Raven
