#pragma once

#include <utility>

namespace Raven
{

class Input
{

public:
    virtual ~Input() = default;

    static bool IsKeyPressed(int keycode);
    static bool IsMouseButtonPressed(int button);
    static std::pair<float, float> GetMousePosition();
    static float GetMouseX();
    static float GetMouseY();

protected:
    virtual bool IsKeyPressedImpl(int keycode) = 0;
    virtual bool IsMousePressedImpl(int button) = 0;
    virtual std::pair<float, float> GetMousePositionImpl() = 0;

protected:
    static Input* s_Instance;

#if 0
public:
    static bool IsKeyPressed(int keycode);

    static bool IsMouseButtonPressed(int button);

    static std::pair<float, float> GetMousePosition();

    static float GetMouseX();
    static float GetMouseY();

#endif

};

}