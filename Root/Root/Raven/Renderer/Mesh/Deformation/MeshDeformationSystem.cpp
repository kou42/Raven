#include "Raven/Renderer/Mesh/Deformation/MeshDeformationSystem.h"

#include "Raven/Physics/PhysicsSimulationWorld.h"
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
    // 従来はMeshDeformationInstance::Update()だけを呼ぶ共通経路でしたが、SoftBodyだけは
    // Fixed Physics Stepへ移管するため、具体型を判定せずMeshDeformerの共通境界からRegistryへ参加させます。
    // Wave / Skeletal / Morph等は引き続き従来の可変dt Update経路を使用します。
    ph::SoftBodyWorld& softBodyWorld = scene.GetPhysicsSimulationWorld().GetSoftBodyWorld();

    // SoftBodyWorldは非所有Registryなので、Entity/Deformer破棄後のpointerを次frameへ残さないよう
    // Game UpdateごとにECSから再構築します。Destroy QueueはPhysics後にflushされるため、
    // このframeで登録したParticipantはFixed Stepと、その直後のMesh同期まで有効です。
    softBodyWorld.Clear();

    for (auto [entity, deformation] : scene.View<MeshDeformationComponent>())
    {
        static_cast<void>(entity);

        // EnabledとInstanceの有効性は責務が異なるため明示的に分けて判定します。
        // Instanceが無効ならSolver/Participant自体へ到達できないため、最初に除外します。
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

            // SimulationとPost-Simulation Mesh同期はPhysicsSimulationWorldへ移管します。
            // このGame Updateでは登録だけを行うため、Fixed Step前の古いPhysics StateをMeshへ書き戻しません。
            softBodyWorld.RegisterSimulationParticipant(*deformer);
            continue;
        }

        // Wave/Skeletal/Morph等、Fixed Physics Stepへ参加しないDeformerは従来の可変dt更新を維持します。
        deformation.Instance->Update(deltaTime);
    }
}

} // namespace Raven
