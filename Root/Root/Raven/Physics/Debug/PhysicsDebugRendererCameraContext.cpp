#include "Raven/Physics/Debug/PhysicsDebugRenderer.h"

namespace Raven::ph
{

PhysicsDebugRenderer::PhysicsDebugRenderer(
    Scene& scene,
    const math::Mat4& view,
    const math::Mat4& projection)
    : m_Scene(&scene)
    , m_FallbackView(&view)
    , m_FallbackProjection(&projection)
{
    // SceneGameのCameraミラー撤去までの移行用Constructorです。
    // 登録処理は通常Constructorと同じにし、既存Sceneの寿命管理を変更しません。
    Registry().push_back(this);
}

} // namespace Raven::ph
