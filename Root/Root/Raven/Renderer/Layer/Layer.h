#pragma once

#include "Raven/Core/Event.h"
#include "Raven/Renderer/RHI/RHITypes.h"

namespace Raven
{

class Scene;
struct UIKeyEvent;

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

    // 全LayerのUpdate / Renderが終わった後、Raven UIのEndFrame直前に呼ばれます。
    // 診断Panelはこの時点のRuntime SnapshotでRetained Widgetを更新し、計測途中の値を表示しません。
    virtual void OnRavenUIRender(float dt) { static_cast<void>(dt); }

    //virtual void OnEvent() {}
    virtual void OnEvent(Event& e) {}

    // Main/補助WindowのUIが未処理にしたキーだけを受ける、明示的な購読口です。
    // 通常OnEventへ補助Window入力を転送せず、RuntimeやMain Viewportへの二重配送を防ぎます。
    // event.Contextは配送中のみ借用でき、Layer側で保持しません。
    virtual bool OnUIKeyEvent(const UIKeyEvent& event);
};

}
