#pragma once
#include <string>
#include <cstddef>
#include <utility>

enum class EventType
{
    None = 0,
    WindowClose,
    WindowResize,
    WindowMoved,
    WindowMinimized,
    WindowRestored,
    WindowMaximized,
    WindowFramebufferResize,
    WindowFocusGained,
    WindowFocusLost,
    KeyPressed,
    KeyReleased,
    CharacterTyped,
    IMEComposition,
    MouseMoved,
    MouseButtonPressed,
    MouseButtonReleased,
    MouseScrolled
};

class Event
{
public:
    virtual ~Event() = default;

    bool Handled = false;

    virtual EventType GetEventType() const = 0;
    virtual std::string ToString() const = 0;
};

class WindowCloseEvent : public Event
{
public:
    EventType GetEventType() const override
    {
        return EventType::WindowClose;
    }

    std::string ToString() const override
    {
        return "WindowCloseEvent";
    }
};

class WindowResizeEvent : public Event
{
public:
    WindowResizeEvent(unsigned int width, unsigned int height)
        : m_Width(width), m_Height(height) {}

    unsigned int GetWidth() const { return m_Width; }
    unsigned int GetHeight() const { return m_Height; }

    EventType GetEventType() const override
    {
        return EventType::WindowResize;
    }

    std::string ToString() const override
    {
        return "WindowResizeEvent: " +
            std::to_string(m_Width) + ", " +
            std::to_string(m_Height);
    }

private:
    unsigned int m_Width;
    unsigned int m_Height;
};

// 論理Window座標での移動通知です。FramebufferのPixel座標とは区別します。
class WindowMovedEvent : public Event
{
public:
    WindowMovedEvent(int x, int y) : m_X(x), m_Y(y) {}
    int GetX() const { return m_X; }
    int GetY() const { return m_Y; }
    EventType GetEventType() const override { return EventType::WindowMoved; }
    std::string ToString() const override
    {
        return "WindowMovedEvent: " + std::to_string(m_X) + ", " + std::to_string(m_Y);
    }
private:
    int m_X;
    int m_Y;
};

class WindowMinimizedEvent : public Event
{
public:
    EventType GetEventType() const override { return EventType::WindowMinimized; }
    std::string ToString() const override { return "WindowMinimizedEvent"; }
};

class WindowRestoredEvent : public Event
{
public:
    EventType GetEventType() const override { return EventType::WindowRestored; }
    std::string ToString() const override { return "WindowRestoredEvent"; }
};

class WindowMaximizedEvent : public Event
{
public:
    EventType GetEventType() const override { return EventType::WindowMaximized; }
    std::string ToString() const override { return "WindowMaximizedEvent"; }
};

// 描画Surfaceの実Pixel数。WindowResizeEventの論理サイズとはDPI環境で異なります。
class WindowFramebufferResizeEvent : public Event
{
public:
    WindowFramebufferResizeEvent(unsigned int width, unsigned int height)
        : m_Width(width), m_Height(height) {}
    unsigned int GetWidth() const { return m_Width; }
    unsigned int GetHeight() const { return m_Height; }
    EventType GetEventType() const override { return EventType::WindowFramebufferResize; }
    std::string ToString() const override
    {
        return "WindowFramebufferResizeEvent: " + std::to_string(m_Width) +
            ", " + std::to_string(m_Height);
    }
private:
    unsigned int m_Width;
    unsigned int m_Height;
};

// Window focusはMouse Captureなど「Window外へ継続できない操作」の終了境界として利用します。
// Platform固有のGLFW focus callbackをCore Eventへ変換し、UIやLayerがWindow実装へ依存しないようにします。
class WindowFocusGainedEvent : public Event
{
public:
    EventType GetEventType() const override
    {
        return EventType::WindowFocusGained;
    }

    std::string ToString() const override
    {
        return "WindowFocusGainedEvent";
    }
};

class WindowFocusLostEvent : public Event
{
public:
    EventType GetEventType() const override
    {
        return EventType::WindowFocusLost;
    }

    std::string ToString() const override
    {
        return "WindowFocusLostEvent";
    }
};

// ============================================================================
// Keyboard Events
// ============================================================================
// Platform固有のKey CodeとModifierをEvent生成時点でsnapshotします。
// ApplicationでUI向けSemantic Keyへ変換することで、UI CoreへGLFW定数を持ち込みません。
class KeyEvent : public Event
{
public:
    int GetKeyCode() const { return m_KeyCode; }
    int GetModifiers() const { return m_Modifiers; }

protected:
    KeyEvent(int keyCode, int modifiers)
        : m_KeyCode(keyCode), m_Modifiers(modifiers) {}

private:
    int m_KeyCode;
    int m_Modifiers;
};

class KeyPressedEvent : public KeyEvent
{
public:
    KeyPressedEvent(int keyCode, int modifiers, bool repeat)
        : KeyEvent(keyCode, modifiers), m_Repeat(repeat) {}

    bool IsRepeat() const { return m_Repeat; }

    EventType GetEventType() const override
    {
        return EventType::KeyPressed;
    }

    std::string ToString() const override
    {
        return "KeyPressedEvent: " + std::to_string(GetKeyCode());
    }

private:
    bool m_Repeat = false;
};

class KeyReleasedEvent : public KeyEvent
{
public:
    KeyReleasedEvent(int keyCode, int modifiers)
        : KeyEvent(keyCode, modifiers) {}

    EventType GetEventType() const override
    {
        return EventType::KeyReleased;
    }

    std::string ToString() const override
    {
        return "KeyReleasedEvent: " + std::to_string(GetKeyCode());
    }
};

// GLFW character callbackから届くUnicode scalar valueを保持します。
// KeyPressedとは分離し、キーボード配列やShiftから文字を推測しません。
class CharacterTypedEvent : public Event
{
public:
    explicit CharacterTypedEvent(unsigned int codepoint) : m_Codepoint(codepoint) {}
    unsigned int GetCodepoint() const { return m_Codepoint; }
    EventType GetEventType() const override { return EventType::CharacterTyped; }
    std::string ToString() const override { return "CharacterTypedEvent: " + std::to_string(m_Codepoint); }
private:
    unsigned int m_Codepoint;
};

// PlatformからのIME通知。UI固有型をCoreへ持ち込まないため独立したEventにします。
// TextはUTF-8、各位置はUnicode codepoint indexです。
enum class IMECompositionEventType
{
    Begin = 0,
    Update,
    Commit,
    End,
    Cancel
};

class IMECompositionEvent : public Event
{
public:
    IMECompositionEvent(IMECompositionEventType type, std::string text = {},
        std::size_t cursor = 0u, std::size_t selectionStart = 0u,
        std::size_t selectionEnd = 0u)
        : m_Type(type), m_Text(std::move(text)), m_Cursor(cursor),
          m_SelectionStart(selectionStart), m_SelectionEnd(selectionEnd) {}

    IMECompositionEventType GetCompositionType() const { return m_Type; }
    const std::string& GetText() const { return m_Text; }
    std::size_t GetCursor() const { return m_Cursor; }
    std::size_t GetSelectionStart() const { return m_SelectionStart; }
    std::size_t GetSelectionEnd() const { return m_SelectionEnd; }
    EventType GetEventType() const override { return EventType::IMEComposition; }
    std::string ToString() const override { return "IMECompositionEvent"; }

private:
    IMECompositionEventType m_Type;
    std::string m_Text;
    std::size_t m_Cursor;
    std::size_t m_SelectionStart;
    std::size_t m_SelectionEnd;
};

// ============================================================================
// Mouse Events
// ============================================================================
// Platform側のMouse入力をApplication / Layerへ流すためのCore Eventです。
// GLFW固有のWindow Handleを上位層へ公開せず、入力が発生した瞬間の情報をEvent自身に保持します。
// 特にButton / Scroll Eventへ座標を含めることで、Consumerが後から現在のCursor位置を再取得する必要をなくし、
// Event生成時点と処理時点で座標がずれる可能性を排除します。
class MouseMovedEvent : public Event
{
public:
    MouseMovedEvent(float x, float y)
        : m_MouseX(x), m_MouseY(y) {}

    float GetX() const { return m_MouseX; }
    float GetY() const { return m_MouseY; }

    EventType GetEventType() const override
    {
        return EventType::MouseMoved;
    }

    std::string ToString() const override
    {
        return "MouseMovedEvent: " + std::to_string(m_MouseX) + ", " + std::to_string(m_MouseY);
    }

private:
    float m_MouseX;
    float m_MouseY;
};

class MouseButtonEvent : public Event
{
public:
    int GetMouseButton() const { return m_Button; }
    float GetX() const { return m_MouseX; }
    float GetY() const { return m_MouseY; }

protected:
    MouseButtonEvent(int button, float x, float y)
        : m_Button(button), m_MouseX(x), m_MouseY(y) {}

private:
    int m_Button;
    float m_MouseX;
    float m_MouseY;
};

class MouseButtonPressedEvent : public MouseButtonEvent
{
public:
    MouseButtonPressedEvent(int button, float x, float y)
        : MouseButtonEvent(button, x, y) {}

    EventType GetEventType() const override
    {
        return EventType::MouseButtonPressed;
    }

    std::string ToString() const override
    {
        return "MouseButtonPressedEvent: " + std::to_string(GetMouseButton()) +
            " at " + std::to_string(GetX()) + ", " + std::to_string(GetY());
    }
};

class MouseButtonReleasedEvent : public MouseButtonEvent
{
public:
    MouseButtonReleasedEvent(int button, float x, float y)
        : MouseButtonEvent(button, x, y) {}

    EventType GetEventType() const override
    {
        return EventType::MouseButtonReleased;
    }

    std::string ToString() const override
    {
        return "MouseButtonReleasedEvent: " + std::to_string(GetMouseButton()) +
            " at " + std::to_string(GetX()) + ", " + std::to_string(GetY());
    }
};

class MouseScrolledEvent : public Event
{
public:
    MouseScrolledEvent(float offsetX, float offsetY, float x, float y)
        : m_OffsetX(offsetX), m_OffsetY(offsetY), m_MouseX(x), m_MouseY(y) {}

    float GetOffsetX() const { return m_OffsetX; }
    float GetOffsetY() const { return m_OffsetY; }
    float GetX() const { return m_MouseX; }
    float GetY() const { return m_MouseY; }

    EventType GetEventType() const override
    {
        return EventType::MouseScrolled;
    }

    std::string ToString() const override
    {
        return "MouseScrolledEvent: " +
            std::to_string(m_OffsetX) + ", " + std::to_string(m_OffsetY) +
            " at " + std::to_string(m_MouseX) + ", " + std::to_string(m_MouseY);
    }

private:
    float m_OffsetX;
    float m_OffsetY;
    float m_MouseX;
    float m_MouseY;
};
