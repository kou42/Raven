#include "Raven/UI/Widgets/UIInputText.h"

#include "Raven/UI/Core/UIContext.h"
#include "Raven/UI/Text/UIUtf8.h"

#include <algorithm>
#include <utility>

namespace Raven
{

math::Vec2 UIInputText::OnMeasureContent() const
{
    return math::Vec2(180.0f, m_Height);
}

float UIInputText::CursorX(std::size_t index) const
{
    float x = m_Padding;
    if (m_Font == nullptr)
    {
        return x;
    }
    std::size_t offset = 0u;
    std::size_t count = 0u;
    std::uint32_t codepoint = 0u;
    while (count < index && UIUtf8::DecodeNext(m_Edit.GetText(), offset, codepoint))
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

std::size_t UIInputText::HitCursor(float localX) const
{
    const std::size_t length = m_Edit.GetLength();
    for (std::size_t index = 0u; index < length; ++index)
    {
        if (localX < (CursorX(index) + CursorX(index + 1u)) * 0.5f)
        {
            return index;
        }
    }
    return length;
}

void UIInputText::NotifyChanged()
{
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
        if (event.Context != nullptr)
        {
            event.Context->SetFocus(this);
        }
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
            // Mouse Down時のAnchorを保持し、Widget外へ出ても選択範囲を更新します。
            m_Edit.MoveCursor(HitCursor(local.x), true);
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

void UIInputText::OnKeyEvent(UIKeyEvent& event)
{
    if (IsFocused() == false || event.Pressed == false)
    {
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
    event.Handled = true;
}

void UIInputText::OnCharacterEvent(UICharacterEvent& event)
{
    if (IsFocused() == true)
    {
        if (m_Edit.InsertCodepoint(event.Codepoint))
        {
            NotifyChanged();
        }
        event.Handled = true;
    }
}

void UIInputText::OnBuildDrawList(UIDrawList& drawList, const math::Vec2& position) const
{
    const math::Vec2 size = GetSize();
    drawList.AddRect(position, position + size,
        ApplyVisualColor(IsFocused() ? math::Vec4{ 0.18f, 0.22f, 0.30f, 1.0f }
                                     : math::Vec4{ 0.13f, 0.13f, 0.16f, 1.0f }));

    if (m_Font != nullptr && m_Font->GetTexture() != nullptr)
    {
        // 背景、選択範囲、文字、Caretの順に積み、既存DrawListのClip/Transformを継承します。
        if (m_Edit.HasSelection())
        {
            const auto selection = m_Edit.GetSelection();
            drawList.AddRect(
                position + math::Vec2(CursorX(selection.first), 3.0f),
                position + math::Vec2(CursorX(selection.second), size.y - 3.0f),
                ApplyVisualColor(math::Vec4{ 0.24f, 0.40f, 0.68f, 0.75f }));
        }
        m_Font->AppendText(drawList, m_Edit.GetText(),
            position + math::Vec2(m_Padding, m_Baseline), m_Height,
            ApplyVisualColor(m_TextColor));
    }
    if (IsFocused())
    {
        const float x = CursorX(m_Edit.GetCursor());
        drawList.AddRect(position + math::Vec2(x, 4.0f),
            position + math::Vec2(x + 1.0f, size.y - 4.0f),
            ApplyVisualColor(m_TextColor));
    }
}

} // namespace Raven
