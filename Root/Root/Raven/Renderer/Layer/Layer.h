#pragma once

namespace Raven
{

class Layer
{
public:
    virtual ~Layer() = default;

    virtual void OnAttach() {}
    virtual void OnDetach() {}
    virtual void OnUpdate() {}
    virtual void OnEvent() {}
};

}