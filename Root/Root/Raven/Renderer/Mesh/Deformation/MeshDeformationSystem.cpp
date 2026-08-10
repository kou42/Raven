#include "Raven/Renderer/Mesh/Deformation/MeshDeformationSystem.h"

#include "Raven/Renderer/Mesh/Deformation/MeshDeformationInstance.h"
#include "Raven/Scene/Components.h"
#include "Raven/Scene/Scene.h"

namespace Raven
{

void MeshDeformationSystem::Update(Scene& scene, float deltaTime)
{
    // ========================================================================
    // ECS -> Deformation bridge
    // ========================================================================
    // ComponentViewがMeshDeformationComponentのStorageだけを走査するため、
    // Meshを持つ全Entityを毎フレーム総当たりする必要はありません。
    //
    // またSystemはMeshDeformationInstance::Update()しか呼ばないため、
    // WaveMeshDeformerをSkeletal/Morph/SoftBodyへ差し替えてもこのコードは変更不要です。
    for (auto [entity, deformation] : scene.View<MeshDeformationComponent>())
    {
        static_cast<void>(entity);

        if (!deformation.Enabled || !deformation.IsValid())
        {
            continue;
        }

        deformation.Instance->Update(deltaTime);
    }
}

} // namespace Raven
