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
    // ECS -> SoftBodyWorld lifetime bridge
    // ========================================================================
    // SoftBodyWorldはSolverを所有しないため、前フレームのpointerを保持し続けると
    // Entity / MeshDeformationInstance破棄後にdangling pointerになる可能性があります。
    // そこで毎Updateの先頭でRegistryを再構築し、このフレームでECSから到達できる
    // SoftBody Solverだけを登録します。登録自体は軽量なpointer列挙で、Physics所有権は移しません。
    ph::SoftBodyWorld& softBodyWorld = scene.GetPhysicsSimulationWorld().GetSoftBodyWorld();
    softBodyWorld.Clear();

    // ========================================================================
    // ECS -> Deformation bridge
    // ========================================================================
    // ComponentViewがMeshDeformationComponentのStorageだけを走査するため、
    // Meshを持つ全Entityを毎フレーム総当たりする必要はありません。
    //
    // MeshDeformer::GetSoftBodySolver()を任意境界として使うため、SystemはCloth / Jellyなどの
    // 具体型を知らず、将来SoftBody Deformerが増えても同じ経路でRegistryへ参加できます。
    for (auto [entity, deformation] : scene.View<MeshDeformationComponent>())
    {
        static_cast<void>(entity);

        if (deformation.IsValid() == false)
        {
            continue;
        }

        MeshDeformer* deformer = deformation.Instance->GetDeformer();
        if (deformer != nullptr)
        {
            ph::SoftBodySolver* softBodySolver = deformer->GetSoftBodySolver();
            if (softBodySolver != nullptr)
            {
                // Enabled=falseでもInstanceが生存している間はRegistryへ残します。
                // Registryは「現在Sceneが所有するSoftBody」を表し、Simulationを進めるかどうかは
                // Deformation側のEnabled判定と、後続のPhysics Step移管時に別責務として扱います。
                softBodyWorld.RegisterSolver(*softBodySolver);
            }
        }

        // 既存のDeformation更新条件は変更しません。
        // SoftBody SolverのStepもまだ各Deformer::Update()内で行うため、二重積分は発生しません。
        if (deformation.Enabled == false)
        {
            continue;
        }

        deformation.Instance->Update(deltaTime);
    }
}

} // namespace Raven
