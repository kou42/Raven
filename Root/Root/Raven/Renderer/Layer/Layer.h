#pragma once

#include "Raven/Core/Event.h"

namespace Raven
{

class Layer
{
public:
    virtual ~Layer() = default;

    virtual void OnAttach() {}
    virtual void OnDetach() {}
    virtual void OnUpdate(float dt) {}
    //virtual void OnUpdate() {}
    virtual void OnRender() {}
    //virtual void OnEvent() {}
    virtual void OnEvent(Event& e) {}
};

}