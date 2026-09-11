#include "Raven/Renderer/Mesh/Deformation/MeshDeformationSystem.h"

#include "Raven/Physics/PhysicsSimulationWorld.h"
#include "Raven/Physics/RigidSoftCouplingComponent.h"
#include "Raven/Renderer/Mesh/Deformation/MeshDeformationInstance.h"
#include "Raven/Scene/Components.h"
#include "Raven/Scene/Scene.h"

namespace Raven
{

void MeshDeformationSystem::Update(Scene& scene, float deltaTime)
{
    // ========================================================================
    // ECS -> Deformation / SoftBody Physics bridge
    // ========================================================================
    // ComponentViewがMeshDeformationComponentのStorageだけを走査するため、
    // Meshを持つ全Entityを毎フレーム総当たりする必要はありません。
    //
    // SoftBody Solver / ParticipantとRigid-Soft Coupling BindingはいずれもRuntime非所有Registryです。
    // EntityやDeformerのlifetimeをRegistry側へ複製せず、Game UpdateごとにECSを正規データとして
    // 再構築することで、Component削除やEntity破棄後のdangling pointerを次frameへ持ち越しません。
    ph::PhysicsSimulationWorld& physicsSimulationWorld = scene.GetPhysicsSimulationWorld();
    ph::SoftBodyWorld& softBodyWorld = physicsSimulationWorld.GetSoftBodyWorld();

    // Destroy QueueはPhysics後にflushされるため、このframeで登録したSolver / Participant / Bindingは
    // Fixed Stepと、その直後のMesh同期まで有効です。
    softBodyWorld.Clear();
    physicsSimulationWorld.ClearRigidSoftSphereColliderBindings();

    for (auto [entity, deformation] : scene.View<MeshDeformationComponent>())
    {
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
            softBodyWorld.RegisterSimulationParticipant(*deformer);

            // ====================================================================
            // ECS Rigid/Soft Coupling -> Runtime Binding
            // ====================================================================
            // Coupling ComponentはSoftBody Entity自身に付与し、Source Rigid Entityと設定値だけを保持します。
            // Solver pointer / Collider IndexはDeformer初期化後にのみ確定するRuntime情報なので、Componentへ
            // 保存せずここで解決します。これによりLayer側の「初期化待ち -> 1回登録 -> 破棄前解除」が不要です。
            const ph::RigidSoftCouplingComponent* coupling =
                scene.TryGetComponent<ph::RigidSoftCouplingComponent>(entity.GetHandle().m_Index);
            if (coupling != nullptr
                && coupling->Enabled == true
                && coupling->SourceRigidEntity.IsValid() == true
                && softBodySolver != nullptr)
            {
                uint32_t colliderIndex = 0u;
                if (deformer->TryGetSoftBodySphereColliderIndex(colliderIndex) == true)
                {
                    ph::RigidSoftSphereColliderBinding binding{};
                    binding.SourceRigidEntity = coupling->SourceRigidEntity;
                    binding.TargetSoftBodyEntity = entity.GetHandle();
                    binding.TargetSolver = softBodySolver;
                    binding.TargetColliderIndex = colliderIndex;
                    binding.ReactionEnabled = coupling->ReactionEnabled;
                    binding.ReactionImpulseScale = coupling->ReactionImpulseScale;
                    binding.MaximumReactionImpulse = coupling->MaximumReactionImpulse;

                    physicsSimulationWorld.RegisterRigidSoftSphereColliderBinding(binding);
                }
            }

            continue;
        }

        // Wave/Skeletal/Morph等、Fixed Physics Stepへ参加しないDeformerは従来の可変dt更新を維持します。
        deformation.Instance->Update(deltaTime);
    }
}

} // namespace Raven
