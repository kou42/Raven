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
    // このframeで登録したParticipantはFixed Step終了まで有効です。
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

            // SimulationだけをPhysicsSimulationWorld::Step()へ移管します。
            // 同一Deformerの重複登録はSoftBodyWorld側で拒否されるため、1 Fixed Stepにつき1回だけ進みます。
            softBodyWorld.RegisterSimulationParticipant(*deformer);

            // 現在のPhysics Stateを描画Meshへ同期します。
            // Sceneの現行順序ではGame UpdateがPhysicsより先なので、Fixed Step後の結果は次frameで反映されます。
            // この1frame遅延は次段階でPost-Physics Synchronize passを追加して解消します。
            deformer->SynchronizeSoftBodyMesh(*mesh);
            continue;
        }

        // Wave/Skeletal/Morph等、Fixed Physics Stepへ参加しないDeformerは従来の可変dt更新を維持します。
        deformation.Instance->Update(deltaTime);
    }
}

} // namespace Raven
