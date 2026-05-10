#pragma once
#include <string>

enum class EventType
{
    None = 0,
    WindowClose,
    WindowResize,
    KeyPressed,
    KeyReleased
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