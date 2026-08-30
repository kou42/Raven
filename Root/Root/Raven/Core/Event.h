#pragma once
#include <string>

enum class EventType
{
    None = 0,
    WindowClose,
    WindowResize,
    KeyPressed,
    KeyReleased,
    MouseMoved,
    MouseButtonPressed,
    MouseButtonReleased
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

// ============================================================================
// Mouse Events
// ============================================================================
// Platform側のMouse入力をApplication / Layerへ流すためのCore Eventです。
// GLFW固有定数やWindow Handleを上位層へ公開せず、座標とButton番号だけを保持します。
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

protected:
    explicit MouseButtonEvent(int button)
        : m_Button(button) {}

private:
    int m_Button;
};

class MouseButtonPressedEvent : public MouseButtonEvent
{
public:
    explicit MouseButtonPressedEvent(int button)
        : MouseButtonEvent(button) {}

    EventType GetEventType() const override
    {
        return EventType::MouseButtonPressed;
    }

    std::string ToString() const override
    {
        return "MouseButtonPressedEvent: " + std::to_string(GetMouseButton());
    }
};

class MouseButtonReleasedEvent : public MouseButtonEvent
{
public:
    explicit MouseButtonReleasedEvent(int button)
        : MouseButtonEvent(button) {}

    EventType GetEventType() const override
    {
        return EventType::MouseButtonReleased;
    }

    std::string ToString() const override
    {
        return "MouseButtonReleasedEvent: " + std::to_string(GetMouseButton());
    }
};