#pragma once

#include "Raven/Core/Event.h"
#include "Raven/Renderer/RHI/RHITypes.h"

namespace Raven
{

class Scene;

class Layer
{
public:
    virtual ~Layer() = default;

    // Backend固有Resourceを直接扱う互換Layerは対応Backendを明示します。
    // 通常LayerはBackend非依存として全Backendで有効です。
    virtual bool SupportsBackend(RHIBackend backend) const
    {
        (void)backend;
        return true;
    }

    virtual void OnAttach() {}
    virtual void OnDetach() {}
    virtual void OnUpdate(float dt) {}

    // Scene破棄前後の通知です。Application-owned LayerがScene Entity / Physics Registryを
    // 安全に解除してから、新Scene向け状態を再構築するために使用します。
    virtual void OnActiveSceneChanging(Scene* scene) { static_cast<void>(scene); }
    virtual void OnActiveSceneChanged(Scene* scene) { static_cast<void>(scene); }
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