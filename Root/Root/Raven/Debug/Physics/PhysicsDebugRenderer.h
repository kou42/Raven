#pragma once

#include "Raven/Debug/Physics/PhysicsDebugSettings.h"

namespace Raven
{
class Scene;
}

namespace Raven::ph
{
class PhysicsWorld;

// ============================================================================
// PhysicsDebugRenderer
// ============================================================================
// PhysicsWorldが公開している診断データをDebugRendererのLineへ変換します。
// Physicsの計算結果は変更しません。
//
// Sceneは現在const版View()を持っていないためScene&を受け取りますが、
// このクラス自身はComponentを書き換えず読み取りだけを行います。
// 将来Scene::View()にconst overloadを追加した段階でconst Scene&へ戻せます。
class PhysicsDebugRenderer
{
public:
    static void Draw(
        Scene& scene,
        const PhysicsWorld& physicsWorld,
        const PhysicsDebugSettings& settings);
};

} // namespace Raven::ph
