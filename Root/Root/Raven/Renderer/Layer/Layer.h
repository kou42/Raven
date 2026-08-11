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

    // Dear ImGuiの1 frameがBegin/Endされている間に呼ばれるEditor/Debug UI描画フックです。
    // 通常のRender処理と分離することで、Runtime LayerがImGuiを必要としない場合は依存を持たず、
    // EditorLayer等の必要なLayerだけがこの関数をoverrideできる構成にします。
    virtual void OnImGuiRender(float dt) {}

    //virtual void OnEvent() {}
    virtual void OnEvent(Event& e) {}
};

}