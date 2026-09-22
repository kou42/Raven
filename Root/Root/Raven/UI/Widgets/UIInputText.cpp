#include "Raven/UI/Widgets/UIInputText.h"

#include "Raven/UI/Core/UIContext.h"
#include "Raven/UI/Text/UIUtf8.h"

#include <algorithm>
#include <utility>

namespace Raven
{

void UIInputText::SetText(std::string_view text)
{
    // 外部Bindingから本文を更新する場合、古い置換範囲をOSへ残さないよう先に変換を取消します。
    if (GetContext() != nullptr)
    {
        GetContext()->CancelIMEComposition(this);
    }
    m_Composition.Cancel();
    m_Edit.SetText(text);
    m_ScrollX = 0.0f;
    InvalidateMeasure();
}

math::Vec2 UIInputText::OnMeasureContent() const
{
    return math::Vec2(180.0f, m_Height);
}

// UTF-8のbyte位置ではなくcodepoint indexから表示上のX座標を求めます。
// Fontに未収録の文字はAppendTextと同じ順序で代替Glyphを探索し、描画との位置ずれを防ぎます。
float UIInputText::CursorX(std::size_t index) const
{
    return TextCursorX(m_Edit.GetText(), index);
}

float UIInputText::TextCursorX(std::string_view text, std::size_t index) const
{
    float x = m_Padding;
    if (m_Font == nullptr)
    {
        return x;
    }
    std::size_t offset = 0u;
    std::size_t count = 0u;
    std::uint32_t codepoint = 0u;
    while (count < index && UIUtf8::DecodeNext(text, offset, codepoint))
    {
        const UIGlyphMetrics* glyph = m_Font->FindGlyph(codepoint);
        if (glyph == nullptr)
        {
            glyph = m_Font->FindGlyph(UIUtf8::ReplacementCharacter);
        }
        if (glyph == nullptr)
        {
            glyph = m_Font->FindGlyph(static_cast<std::uint32_t>('?'));
        }
        if (glyph != nullptr)
        {
            x += glyph->Advance;
        }
        ++count;
    }
    return x;
}


// Compositionは選択範囲を視覚的に置換するだけで、確定済みTextを変更しません。
// UTF-8のbyte境界はcodepoint走査で求め、複数byte文字の途中を切断しません。
std::string UIInputText::GetDisplayText() const
{
    if (m_Composition.IsActive() == false)
    {
        return m_Edit.GetText();
    }

    const std::string& text = m_Edit.GetText();
    const std::size_t startIndex = m_Composition.GetReplacementStart();
    const std::size_t endIndex = m_Composition.GetReplacementEnd();
    std::size_t offset = 0u;
    std::size_t index = 0u;
    std::uint32_t codepoint = 0u;
    while (index < startIndex && UIUtf8::DecodeNext(text, offset, codepoint))
    {
        ++index;
    }
    const std::size_t begin = offset;
    while (index < endIndex && UIUtf8::DecodeNext(text, offset, codepoint))
    {
        ++index;
    }

    std::string display = text;
    display.replace(begin, offset - begin, m_Composition.GetText());
    return display;
}

math::Vec2 UIInputText::GetIMECaretScreenPosition() const
{
    EnsureCursorVisible();
    const std::string display = GetDisplayText();
    const float x = std::clamp(TextCursorX(display, GetDisplayCursor()) - m_ScrollX,
        m_Padding, std::max(m_Padding, GetSize().x - m_Padding));
    return LocalToScreenPosition(math::Vec2(x, GetSize().y - 2.0f));
}

std::size_t UIInputText::GetDisplayCursor() const
{
    if (m_Composition.IsActive() == true)
    {
        return m_Composition.GetReplacementStart() + m_Composition.GetCursor();
    }
    return m_Edit.GetCursor();
}

// Caretの位置を優先して可視範囲を決めます。const描画経路からも呼ぶためScrollのみmutableです。
// 内容が短くなった場合やWidgetの幅が広がった場合はmaxScrollで余分なOffsetを戻します。
void UIInputText::EnsureCursorVisible() const
{
    const float width = std::max(0.0f, GetSize().x - m_Padding * 2.0f);
    const std::string display = GetDisplayText();
    std::size_t displayLength = 0u;
    std::size_t offset = 0u;
    std::uint32_t codepoint = 0u;
    while (UIUtf8::DecodeNext(display, offset, codepoint))
    {
        ++displayLength;
    }
    const float contentWidth = std::max(0.0f, TextCursorX(display, displayLength) - m_Padding);
    const float maxScroll = std::max(0.0f, contentWidth - width);
    const float caret = TextCursorX(display, GetDisplayCursor()) - m_Padding;
    // Cursorを常に表示範囲へ収め、削除・Undo・リサイズ時は余ったScrollを戻します。
    m_ScrollX = std::clamp(m_ScrollX, 0.0f, maxScroll);
    if (caret < m_ScrollX)
    {
        m_ScrollX = caret;
    }
    else if (caret > m_ScrollX + width)
    {
        m_ScrollX = caret - width;
    }
    m_ScrollX = std::clamp(m_ScrollX, 0.0f, maxScroll);
}

std::size_t UIInputText::HitCursor(float localX) const
{
    // 各文字のCursorXを先頭から再計算すると長文でO(n^2)になるため、
    // UTF-8を一度だけ走査し、Glyph Advanceを累積してHit位置を判定します。
    const std::string& text = m_Edit.GetText();
    const float targetX = localX + m_ScrollX;
    float penX = m_Padding;
    std::size_t offset = 0u;
    std::size_t index = 0u;
    std::uint32_t codepoint = 0u;
    while (UIUtf8::DecodeNext(text, offset, codepoint))
    {
        float advance = 0.0f;
        if (m_Font != nullptr)
        {
            const UIGlyphMetrics* glyph = m_Font->FindGlyph(codepoint);
            if (glyph == nullptr)
            {
                glyph = m_Font->FindGlyph(UIUtf8::ReplacementCharacter);
            }
            if (glyph == nullptr)
            {
                glyph = m_Font->FindGlyph(static_cast<std::uint32_t>('?'));
            }
            if (glyph != nullptr)
            {
                advance = glyph->Advance;
            }
        }
        if (targetX < penX + advance * 0.5f)
        {
            return index;
        }
        penX += advance;
        ++index;
    }
    return index;
}

// InputNumber等の制約を「挿入後の全文」に対して評価します。
// 選択文字列の置換・貼り付けでも、不正な候補を本体のUndo履歴へ残しません。
bool UIInputText::InsertFiltered(std::string_view text)
{
    if (m_InputFilter != nullptr)
    {
        // 実際のBufferを変更する前に選択範囲の置換を含めた完成形を検査します。
        // 拒否時にUndo履歴やCursor位置を変更しないため、貼り付けも原子的に扱えます。
        UITextEditBuffer candidate = m_Edit;
        if (candidate.InsertText(text) == false ||
            m_InputFilter(candidate.GetText()) == false)
        {
            return false;
        }
    }
    return m_Edit.InsertText(text);
}

// IME確定結果はUTF-8文字列として一括挿入します。
// Composition開始時の選択範囲を復元してから一度だけInsertTextするため、
// 日本語の複数文字確定・選択範囲置換ともUndo一回で元に戻せます。
// Filter拒否時には本文・選択・Undo履歴を変更しません。
bool UIInputText::CommitIMEText(std::string_view text)
{
    UITextEditBuffer candidate = m_Edit;
    if (m_Composition.IsActive() == true)
    {
        const std::size_t begin = m_Composition.GetReplacementStart();
        const std::size_t end = m_Composition.GetReplacementEnd();
        candidate.MoveCursor(begin);
        candidate.MoveCursor(end, true);
    }

    if (candidate.InsertText(text) == false ||
        (m_InputFilter != nullptr && m_InputFilter(candidate.GetText()) == false))
    {
        m_Composition.End();
        EnsureCursorVisible();
        return false;
    }

    // Candidateを直接代入するとUndo履歴までコピーするため、
    // 検証後に本体へ同じ編集を一度だけ適用します。
    if (m_Composition.IsActive() == true)
    {
        const std::size_t begin = m_Composition.GetReplacementStart();
        const std::size_t end = m_Composition.GetReplacementEnd();
        m_Edit.MoveCursor(begin);
        m_Edit.MoveCursor(end, true);
    }
    const bool inserted = m_Edit.InsertText(text);
    m_Composition.End();
    if (inserted == true)
    {
        NotifyChanged();
    }
    return inserted;
}

void UIInputText::NotifyChanged()
{
    EnsureCursorVisible();
    InvalidateMeasure();
    if (m_OnChange != nullptr)
    {
        m_OnChange(m_Edit.GetText());
    }
}

void UIInputText::OnMouseEvent(UIMouseEvent& event)
{
    if (event.Target != this)
    {
        return;
    }

    if (event.Type == UIMouseEventType::Cancel)
    {
        // Window Focus Lost / Tree破棄時はMouse Upが来ないため、選択状態だけ確実に終了します。
        m_Selecting = false;
        event.Handled = true;
        return;
    }

    if (event.Type == UIMouseEventType::Down && event.Button == UIMouseButton::Left)
    {
        // 編集位置を変更する操作では古いIME置換範囲を無効化します。
        m_Composition.Cancel();
        if (event.Context != nullptr)
        {
            event.Context->SetFocus(this);
        }
        EnsureCursorVisible();
        math::Vec2 local{};
        if (TryScreenToLocalPosition(event.ScreenPosition, local))
        {
            m_Edit.MoveCursor(HitCursor(local.x));
        }

        // Captureが他Widgetに占有されている場合はDragを開始しません。
        m_Selecting = event.Context != nullptr && event.Context->CaptureMouse(this);
        event.Handled = true;
        return;
    }

    if (m_Selecting == false)
    {
        return;
    }

    if (event.Type == UIMouseEventType::Move ||
        (event.Type == UIMouseEventType::Up && event.Button == UIMouseButton::Left))
    {
        math::Vec2 local{};
        if (TryScreenToLocalPosition(event.ScreenPosition, local))
        {
            // Capture中に左右の表示領域を越えた場合は、選択を維持しつつ横スクロールします。
            // Pointerのはみ出し量を一度のMoveで進める量とし、内容の両端では停止します。
            const float right = std::max(m_Padding, GetSize().x - m_Padding);
            const float contentWidth = std::max(0.0f, CursorX(m_Edit.GetLength()) - m_Padding);
            const float visibleWidth = std::max(0.0f, GetSize().x - m_Padding * 2.0f);
            const float maxScroll = std::max(0.0f, contentWidth - visibleWidth);
            if (local.x < m_Padding)
            {
                m_ScrollX = std::max(0.0f, m_ScrollX - (m_Padding - local.x));
            }
            else if (local.x > right)
            {
                m_ScrollX = std::min(maxScroll, m_ScrollX + local.x - right);
            }
            m_Edit.MoveCursor(HitCursor(std::clamp(local.x, m_Padding, right)), true);
        }
        if (event.Type == UIMouseEventType::Up)
        {
            m_Selecting = false;
            if (event.Context != nullptr)
            {
                event.Context->ReleaseMouseCapture(this);
            }
        }
        event.Handled = true;
    }
}

// UIInputNumberはFocus喪失を確定境界として利用します。
// Focus取得時には通知せず、確定処理の重複を避けます。
void UIInputText::OnFocusChanged(bool focused)
{
    if (focused == false)
    {
        // Focus喪失時に未確定表示を残さず、確定済みTextとUndo履歴には触れません。
        m_Composition.Cancel();
        if (m_OnFocusLost != nullptr)
        {
            m_OnFocusLost();
        }
    }
}

// 物理キー由来の編集・ショートカットだけを処理します。
// 日本語等の確定文字はOnCharacterEventへ分離し、キー押下で二重挿入しません。
void UIInputText::OnKeyEvent(UIKeyEvent& event)
{
    if (IsFocused() == false || event.Pressed == false)
    {
        return;
    }

    // 変換中の矢印・Enter・Backspace等はIMEが所有します。
    // ここで通常編集を行うと、未確定文字列と置換対象範囲が食い違います。
    if (m_Composition.IsActive() == true)
    {
        return;
    }

    const bool shortcut = event.Control == true || event.Super == true;
    if (shortcut == true)
    {
        if (event.Key == UIKey::A)
        {
            m_Edit.SelectAll();
        }
        else if (event.Key == UIKey::C || event.Key == UIKey::X)
        {
            if (m_WriteClipboard != nullptr && m_Edit.HasSelection() == true)
            {
                m_WriteClipboard(m_Edit.GetSelectedText());
                if (event.Key == UIKey::X && m_Edit.DeleteSelection())
                {
                    NotifyChanged();
                }
            }
        }
        else if (event.Key == UIKey::V)
        {
            if (m_ReadClipboard != nullptr && InsertFiltered(m_ReadClipboard()))
            {
                NotifyChanged();
            }
        }
        else if (event.Key == UIKey::Z || event.Key == UIKey::Y)
        {
            const bool redo = event.Key == UIKey::Y ||
                (event.Key == UIKey::Z && event.Shift == true);
            if ((redo == true ? m_Edit.Redo() : m_Edit.Undo()) == true)
            {
                NotifyChanged();
            }
        }
        else
        {
            return;
        }
        event.Handled = true;
        return;
    }

    if (event.Key == UIKey::Enter && m_OnSubmit != nullptr)
    {
        m_OnSubmit();
        event.Handled = true;
        return;
    }

    if ((event.Key == UIKey::Up || event.Key == UIKey::Down) &&
        m_OnStep != nullptr)
    {
        m_OnStep(event.Key == UIKey::Up);
        event.Handled = true;
        return;
    }

    const std::size_t cursor = m_Edit.GetCursor();
    if (event.Key == UIKey::Left)
    {
        m_Edit.MoveCursor(cursor > 0u ? cursor - 1u : 0u, event.Shift);
    }
    else if (event.Key == UIKey::Right)
    {
        m_Edit.MoveCursor(std::min(cursor + 1u, m_Edit.GetLength()), event.Shift);
    }
    else if (event.Key == UIKey::Home)
    {
        m_Edit.MoveCursor(0u, event.Shift);
    }
    else if (event.Key == UIKey::End)
    {
        m_Edit.MoveCursor(m_Edit.GetLength(), event.Shift);
    }
    else if (event.Key == UIKey::Backspace)
    {
        if (m_Edit.Backspace())
        {
            NotifyChanged();
        }
    }
    else if (event.Key == UIKey::Delete)
    {
        if (m_Edit.DeleteForward())
        {
            NotifyChanged();
        }
    }
    else
    {
        return;
    }
    EnsureCursorVisible();
    event.Handled = true;
}

void UIInputText::OnCharacterEvent(UICharacterEvent& event)
{
    if (IsFocused() == true)
    {
        // PlatformがEndより先に確定Characterを通知しても、確定文字は従来経路で受け取ります。
        // OS側のResult文字列とGLFW Characterの二重通知はPlatform Adapterで排他制御します。
        if (m_Composition.IsActive() == true)
        {
            m_Composition.End();
        }
        UITextEditBuffer character;
        if (character.InsertCodepoint(event.Codepoint) &&
            InsertFiltered(character.GetText()))
        {
            NotifyChanged();
        }
        event.Handled = true;
    }
}


void UIInputText::OnIMEEvent(UIIMEEvent& event)
{
    if (IsFocused() == false)
    {
        return;
    }

    if (event.Type == UIIMEEventType::Begin)
    {
        const auto selection = m_Edit.GetSelection();
        m_Composition.Begin(selection.first, selection.second);
    }
    else if (event.Type == UIIMEEventType::Update)
    {
        if (m_Composition.IsActive() == false)
        {
            const auto selection = m_Edit.GetSelection();
            m_Composition.Begin(selection.first, selection.second);
        }
        m_Composition.Update(event.Text, event.Cursor, event.SelectionStart, event.SelectionEnd);
    }
    else if (event.Type == UIIMEEventType::Commit)
    {
        // Platformはこの確定結果をCharacter Eventで重複配送しない契約です。
        CommitIMEText(event.Text);
    }
    else if (event.Type == UIIMEEventType::End)
    {
        m_Composition.End();
    }
    else if (event.Type == UIIMEEventType::Cancel)
    {
        m_Composition.Cancel();
    }
    else
    {
        return;
    }

    EnsureCursorVisible();
    event.Handled = true;
}

// UIElement側のClipSelfで入力欄の外へはみ出す文字・選択矩形・Caretを切り取ります。
// ここではScrollを各描画座標へ反映し、背景だけは固定位置に描きます。
void UIInputText::OnBuildDrawList(UIDrawList& drawList, const math::Vec2& position) const
{
    const math::Vec2 size = GetSize();
    EnsureCursorVisible();
    // Themeは描画時に参照し、Retained Treeを作り直さず次frameから切替を反映します。
    // Contextを持たない単体Widgetは従来と同じDark配色を使います。
    const UIContext* context = GetContext();
    const UIInputTextStyle defaultStyle;
    const UIInputTextStyle& style = context != nullptr ? context->GetTheme().InputText : defaultStyle;
    const math::Vec4& textColor = m_TextColorOverride == true ? m_TextColor : style.TextColor;
    drawList.AddRect(position, position + size,
        ApplyVisualColor(IsFocused() == true ? style.FocusedBackgroundColor : style.BackgroundColor));

    const std::string display = GetDisplayText();
    if (m_Font != nullptr && m_Font->GetTexture() != nullptr)
    {
        // 背景、選択範囲、文字、Caretの順に積み、既存DrawListのClip/Transformを継承します。
        if (m_Edit.HasSelection() && m_Composition.IsActive() == false)
        {
            const auto selection = m_Edit.GetSelection();
            drawList.AddRect(
                position + math::Vec2(CursorX(selection.first) - m_ScrollX, 3.0f),
                position + math::Vec2(CursorX(selection.second) - m_ScrollX, size.y - 3.0f),
                ApplyVisualColor(style.SelectionColor));
        }
        if (m_Composition.IsActive() == true)
        {
            // IMEが選択中の変換候補範囲を本文の選択とは独立して装飾します。
            // indexはComposition文字列のcodepoint単位なので、表示上の置換開始を加算します。
            const auto selection = m_Composition.GetSelection();
            if (selection.second > selection.first)
            {
                const std::size_t begin = m_Composition.GetReplacementStart();
                const float left = TextCursorX(display, begin + selection.first) - m_ScrollX;
                const float right = TextCursorX(display, begin + selection.second) - m_ScrollX;
                drawList.AddRect(position + math::Vec2(left, 3.0f),
                    position + math::Vec2(right, size.y - 3.0f),
                    ApplyVisualColor(style.IMESelectionColor));
            }
        }
        m_Font->AppendText(drawList, display,
            position + math::Vec2(m_Padding - m_ScrollX, m_Baseline), m_Height,
            ApplyVisualColor(textColor));

        if (m_Composition.IsActive() == true)
        {
            // IME未確定範囲だけに下線を引き、確定済みTextとは視覚的に区別します。
            const std::size_t begin = m_Composition.GetReplacementStart();
            std::size_t length = 0u;
            std::size_t offset = 0u;
            std::uint32_t codepoint = 0u;
            while (UIUtf8::DecodeNext(m_Composition.GetText(), offset, codepoint))
            {
                ++length;
            }
            const float left = TextCursorX(display, begin) - m_ScrollX;
            const float right = TextCursorX(display, begin + length) - m_ScrollX;
            if (right > left)
            {
                drawList.AddRect(position + math::Vec2(left, size.y - 4.0f),
                    position + math::Vec2(right, size.y - 2.0f),
                    ApplyVisualColor(textColor));
            }
        }
    }
    if (IsFocused())
    {
        const float x = TextCursorX(display, GetDisplayCursor()) - m_ScrollX;
        drawList.AddRect(position + math::Vec2(x, 4.0f),
            position + math::Vec2(x + 1.0f, size.y - 4.0f),
            ApplyVisualColor(textColor));
    }
}

} // namespace Raven
