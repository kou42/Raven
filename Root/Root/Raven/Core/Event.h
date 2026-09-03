#pragma once
#include <string>

enum class EventType
{
    None = 0,
    WindowClose,
    WindowResize,
    WindowFocusGained,
    WindowFocusLost,
    KeyPressed,
    KeyReleased,
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
    EventType GetEventType() const override { return EventType::WindowClose; }
    std::string ToString() const override { return "WindowCloseEvent"; }
};

class WindowResizeEvent : public Event
{
public:
    WindowResizeEvent(unsigned int width, unsigned int height) : m_Width(width), m_Height(height) {}
    unsigned int GetWidth() const { return m_Width; }
    unsigned int GetHeight() const { return m_Height; }
    EventType GetEventType() const override { return EventType::WindowResize; }
    std::string ToString() const override { return "WindowResizeEvent: " + std::to_string(m_Width) + ", " + std::to_string(m_Height); }
private:
    unsigned int m_Width;
    unsigned int m_Height;
};

class WindowFocusGainedEvent : public Event
{
public:
    EventType GetEventType() const override { return EventType::WindowFocusGained; }
    std::string ToString() const override { return "WindowFocusGainedEvent"; }
};

class WindowFocusLostEvent : public Event
{
public:
    EventType GetEventType() const override { return EventType::WindowFocusLost; }
    std::string ToString() const override { return "WindowFocusLostEvent"; }
};

// Platform固有のKey codeはintのsnapshotとして保持し、Shift等のModifierも入力発生時点で固定します。
class KeyEvent : public Event
{
public:
    int GetKeyCode() const { return m_KeyCode; }
    int GetModifiers() const { return m_Modifiers; }
protected:
    KeyEvent(int keyCode, int modifiers) : m_KeyCode(keyCode), m_Modifiers(modifiers) {}
private:
    int m_KeyCode;
    int m_Modifiers;
};

class KeyPressedEvent : public KeyEvent
{
public:
    KeyPressedEvent(int keyCode, int modifiers, bool repeat) : KeyEvent(keyCode, modifiers), m_Repeat(repeat) {}
    bool IsRepeat() const { return m_Repeat; }
    EventType GetEventType() const override { return EventType::KeyPressed; }
    std::string ToString() const override { return "KeyPressedEvent: " + std::to_string(GetKeyCode()); }
private:
    bool m_Repeat = false;
};

class KeyReleasedEvent : public KeyEvent
{
public:
    KeyReleasedEvent(int keyCode, int modifiers) : KeyEvent(keyCode, modifiers) {}
    EventType GetEventType() const override { return EventType::KeyReleased; }
    std::string ToString() const override { return "KeyReleasedEvent: " + std::to_string(GetKeyCode()); }
};

class MouseMovedEvent : public Event
{
public:
    MouseMovedEvent(float x, float y) : m_MouseX(x), m_MouseY(y) {}
    float GetX() const { return m_MouseX; }
    float GetY() const { return m_MouseY; }
    EventType GetEventType() const override { return EventType::MouseMoved; }
    std::string ToString() const override { return "MouseMovedEvent: " + std::to_string(m_MouseX) + ", " + std::to_string(m_MouseY); }
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
    MouseButtonEvent(int button, float x, float y) : m_Button(button), m_MouseX(x), m_MouseY(y) {}
private:
    int m_Button;
    float m_MouseX;
    float m_MouseY;
};

class MouseButtonPressedEvent : public MouseButtonEvent
{
public:
    MouseButtonPressedEvent(int button, float x, float y) : MouseButtonEvent(button, x, y) {}
    EventType GetEventType() const override { return EventType::MouseButtonPressed; }
    std::string ToString() const override { return "MouseButtonPressedEvent: " + std::to_string(GetMouseButton()) + " at " + std::to_string(GetX()) + ", " + std::to_string(GetY()); }
};

class MouseButtonReleasedEvent : public MouseButtonEvent
{
public:
    MouseButtonReleasedEvent(int button, float x, float y) : MouseButtonEvent(button, x, y) {}
    EventType GetEventType() const override { return EventType::MouseButtonReleased; }
    std::string ToString() const override { return "MouseButtonReleasedEvent: " + std::to_string(GetMouseButton()) + " at " + std::to_string(GetX()) + ", " + std::to_string(GetY()); }
};

class MouseScrolledEvent : public Event
{
public:
    MouseScrolledEvent(float offsetX, float offsetY, float x, float y) : m_OffsetX(offsetX), m_OffsetY(offsetY), m_MouseX(x), m_MouseY(y) {}
    float GetOffsetX() const { return m_OffsetX; }
    float GetOffsetY() const { return m_OffsetY; }
    float GetX() const { return m_MouseX; }
    float GetY() const { return m_MouseY; }
    EventType GetEventType() const override { return EventType::MouseScrolled; }
    std::string ToString() const override { return "MouseScrolledEvent: " + std::to_string(m_OffsetX) + ", " + std::to_string(m_OffsetY) + " at " + std::to_string(m_MouseX) + ", " + std::to_string(m_MouseY); }
private:
    float m_OffsetX;
    float m_OffsetY;
    float m_MouseX;
    float m_MouseY;
};