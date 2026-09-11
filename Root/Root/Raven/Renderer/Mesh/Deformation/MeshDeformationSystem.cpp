#include "Raven/Renderer/Mesh/Deformation/MeshDeformationSystem.h"

#include "Raven/Physics/PhysicsSimulationWorld.h"
#include "Raven/Renderer/Mesh/Deformation/MeshDeformationInstance.h"
#include "Raven/Scene/Components.h"
#include "Raven/Scene/Scene.h"

namespace Raven
{

void MeshDeformationSystem::Update(Scene& scene, float deltaTime)
{
    ph::SoftBodyWorld& softBodyWorld = scene.GetPhysicsSimulationWorld().GetSoftBodyWorld();

    // SoftBodyWorldは非所有Registryなので、Entity/Deformer破棄後のpointerを次frameへ残さないよう
    // Game UpdateごとにECSから再構築します。Destroy QueueはPhysics後にflushされるため、
    // このframeで登録したParticipantはFixed Stepと、その直後のMesh同期まで有効です。
    softBodyWorld.Clear();

    for (auto [entity, deformation] : scene.View<MeshDeformationComponent>())
    {
        static_cast<void>(entity);

        if (deformation.IsValid() == false)
        {
            continue;
        }

        MeshDeformer* deformer = deformation.Instance->GetDeformer();
        if (deformer == nullptr)
        {
            continue;
        }

        ph::SoftBodySolver* softBodySolver = deformer->GetSoftBodySolver();
        if (softBodySolver != nullptr)
        {
            // Solver RegistryはDebug/Coupling用なので、Enabled=falseでもSceneに存在するSolverを保持します。
            softBodyWorld.RegisterSolver(*softBodySolver);
        }

        if (deformation.Enabled == false)
        {
            continue;
        }

        if (deformer->HasSeparatedSoftBodyUpdate() == true)
        {
            const Ref<Mesh>& mesh = deformation.Instance->GetMesh();
            if (mesh == nullptr)
            {
                continue;
            }

            // ClothのMesh依存初期化はGame/Renderer側で済ませ、Physics Fixed StepからMeshへ逆依存させません。
            if (deformer->PrepareSoftBodySimulation(*mesh) == false)
            {
                continue;
            }

            // Physics側はRenderer型を参照せずParticipantだけを扱うため、同期対象Meshはここで事前にbindします。
            // MeshDeformationInstanceがMesh/Deformerを同時所有するため、登録frame中はpointer lifetimeが一致します。
            deformer->BindSoftBodySynchronizationMesh(*mesh);

            // SimulationとPost-Simulation Mesh同期はPhysicsSimulationWorld::Step()へ移管します。
            // このGame Updateでは登録だけを行うため、Fixed Step前の古いPhysics StateをMeshへ書き戻しません。
            softBodyWorld.RegisterSimulationParticipant(*deformer);
            continue;
        }

        // Wave/Skeletal/Morph等、Fixed Physics Stepへ参加しないDeformerは従来の可変dt更新を維持します。
        deformation.Instance->Update(deltaTime);
    }
}

} // namespace Raven
