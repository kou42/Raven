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
        m_Undo.clear();
        m_Redo.clear();
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

    // Shift操作中はAnchorを固定してCursorだけ動かします。
    // 選択範囲は両者の大小から求めるため、逆方向へのドラッグにも対応します。
    void MoveCursor(std::size_t index, bool extendSelection = false)
    {
        m_Cursor = std::min(index, GetLength());
        if (extendSelection == false)
        {
            m_Anchor = m_Cursor;
        }
    }

    std::string GetSelectedText() const
    {
        const auto selection = GetSelection();
        const std::size_t begin = ByteOffset(selection.first);
        return m_Text.substr(begin, ByteOffset(selection.second) - begin);
    }

    // Undo/Redoは文字列だけでなくCursor/Anchorも保存し、選択範囲を正確に復元します。
    bool Undo()
    {
        if (m_Undo.empty())
        {
            return false;
        }
        m_Redo.push_back(Snapshot());
        Restore(m_Undo.back());
        m_Undo.pop_back();
        return true;
    }

    bool Redo()
    {
        if (m_Redo.empty())
        {
            return false;
        }
        m_Undo.push_back(Snapshot());
        Restore(m_Redo.back());
        m_Redo.pop_back();
        return true;
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

    // Clipboard等から届くUTF-8を正規化し、単一行に入れられない制御文字を除外します。
    // Undoは置換前に一度だけ記録するため、選択範囲の置換を一操作で戻せます。
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
        RecordEdit();
        m_Text.replace(begin, end - begin, normalized);
        m_Cursor = selection.first + CountCodepoints(normalized);
        m_Anchor = m_Cursor;
        return true;
    }

    // Cursorはcodepoint indexなので、削除前にbyte境界へ変換します。
    // UTF-8の途中のbyteだけを消して文字列を壊さないための処理です。
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
        RecordEdit();
        m_Text.erase(begin, end - begin);
        --m_Cursor;
        m_Anchor = m_Cursor;
        return true;
    }

    // Backspaceと対称に、選択範囲がなければCursorの次のcodepointを削除します。
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
        RecordEdit();
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
        RecordEdit();
        m_Text.erase(begin, end - begin);
        m_Cursor = selection.first;
        m_Anchor = m_Cursor;
        return true;
    }

private:
    struct EditState
    {
        std::string Text;
        std::size_t Cursor = 0u;
        std::size_t Anchor = 0u;
    };

    EditState Snapshot() const { return { m_Text, m_Cursor, m_Anchor }; }
    void Restore(const EditState& state)
    {
        m_Text = state.Text;
        m_Cursor = state.Cursor;
        m_Anchor = state.Anchor;
    }
    // 編集が発生した時点でRedo分岐を破棄します。
    // Cursor移動や選択だけでは呼ばず、Undo履歴を文字列変更単位に保ちます。
    void RecordEdit()
    {
        // 履歴は有限長とし、長時間のEditor利用で無制限に増えないようにします。
        constexpr std::size_t maxHistory = 128u;
        if (m_Undo.size() >= maxHistory)
        {
            m_Undo.erase(m_Undo.begin());
        }
        m_Undo.push_back(Snapshot());
        m_Redo.clear();
    }

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

    std::vector<EditState> m_Undo;
    std::vector<EditState> m_Redo;
    std::string m_Text;
    std::size_t m_Cursor = 0u;
    std::size_t m_Anchor = 0u;
};

} // namespace Raven
