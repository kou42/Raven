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
// Physicsの計算結果を変更せず、読み取り専用で可視化することが重要です。
class PhysicsDebugRenderer
{
public:
    static void Draw(
        const Scene& scene,
        const PhysicsWorld& physicsWorld,
        const PhysicsDebugSettings& settings);
};

} // namespace Raven::ph
