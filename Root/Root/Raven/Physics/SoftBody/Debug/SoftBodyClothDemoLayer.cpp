#include "Raven/Physics/SoftBody/Debug/SoftBodyClothDemoLayer.h"

#include <cmath>

#include "Raven/Core/Application.h"
#include "Raven/Renderer/Material/Material.h"
#include "Raven/Renderer/Mesh/Deformation/MeshDeformationInstance.h"
#include "Raven/Renderer/Mesh/Deformation/SoftBodyClothDeformer.h"
#include "Raven/Renderer/Mesh/PrimitiveMeshFactory.h"
#include "Raven/Renderer/Pipeline/Pipeline.h"
#include "Raven/Renderer/Shader/Shader.h"
#include "Raven/Scene/Components.h"
#include "Raven/Scene/Scene.h"

namespace Raven
{
namespace
{
constexpr uint32_t kClothRows = 24u;
constexpr uint32_t kClothColumns = 24u;

// Solverは小さなローカル座標で解き、Entity TransformだけでWorldへ配置します。
// Physicsの数値スケールとSceneの見た目スケールを分離することが重要です。
constexpr math::Vec3 kClothWorldPosition{ 0.0f, 18.0f, -10.0f };
constexpr float kClothWorldScale = 22.0f;
constexpr float kFloorLocalY = -18.0f / 22.0f;

constexpr math::Vec3 kRigidSphereInitialLocalCenter{ 0.0f, 0.0f, 0.36f };
constexpr float kRigidSphereWorldRadius = 2.4f;
constexpr float kRigidSphereMass = 4.0f;
constexpr math::Vec3 kRigidSphereInitialVelocity{ 0.0f, 0.0f, -8.0f };

constexpr float kSoftRigidReactionScale = 0.12f;
constexpr float kMaxReactionImpulse = 18.0f;

math::Vec3 ClothLocalToWorldPosition(const math::Vec3& localPosition)
{
    return kClothWorldPosition + localPosition * kClothWorldScale;
}

math::Vec3 WorldToClothLocalPosition(const math::Vec3& worldPosition)
{
    return (worldPosition - kClothWorldPosition) / kClothWorldScale;
}

float WorldToClothLocalLength(float worldLength)
{
    return worldLength / kClothWorldScale;
}

math::Vec3 ClampMagnitude(const math::Vec3& value, float maxMagnitude)
{
    const float lengthSq = value.LengthSq();
    const float maxMagnitudeSq = maxMagnitude * maxMagnitude;

    if (lengthSq <= maxMagnitudeSq || lengthSq <= math::Epsilon * math::Epsilon)
    {
        return value;
    }

    return value * (maxMagnitude / std::sqrt(lengthSq));
}
} // namespace

void SoftBodyClothDemoLayer::OnAttach()
{
    Scene* scene = m_Application.GetScene();
    if (scene == nullptr)
    {
        return;
    }

    m_ClothMesh = PrimitiveMeshFactory::CreateDynamicGrid(
        static_cast<int>(kClothRows),
        static_cast<int>(kClothColumns));
    m_CollisionSphereMesh = PrimitiveMeshFactory::CreateSphere();

    if (m_ClothMesh == nullptr || m_CollisionSphereMesh == nullptr)
    {
        m_ClothMesh.reset();
        m_CollisionSphereMesh.reset();
        return;
    }

    ShaderLibrary shaderLibrary{};
    Ref<Shader> shader = shaderLibrary.Load(
        "SoftBodyClothDemo",
        "Raven/Assets/Shaders/Vertex/test.vert",
        "Raven/Assets/Shaders/Fragment/test.frag");

    if (shader == nullptr)
    {
        m_ClothMesh.reset();
        m_CollisionSphereMesh.reset();
        return;
    }

    PipelineSpecification pipelineSpecification{};
    pipelineSpecification.DebugName = "SoftBody Cloth Demo Pipeline";
    pipelineSpecification.Shader = shader;
    pipelineSpecification.Topology = PrimitiveTopology::Triangles;
    pipelineSpecification.Cull = CullMode::None;
    pipelineSpecification.FrontFaceMode = FrontFace::CounterClockwise;
    pipelineSpecification.DepthTest = true;
    pipelineSpecification.DepthWrite = true;
    pipelineSpecification.DepthCompare = DepthCompareOperator::Less;
    pipelineSpecification.Blend = true;

    // MaterialをEntityごとに分ける理由は、u_TintがMaterialの可変状態だからです。
    // ClothとSphereが同じMaterialを共有すると、描画順によって色が上書きされます。
    m_ClothMaterial = CreateRef<Material>(Pipeline::Create(pipelineSpecification));
    m_ClothMaterial->SetUniform("u_Tint", math::Vec3{ 0.35f, 0.65f, 1.0f });
    m_ClothMaterial->SetUniform("u_Alpha", 1.0f);

    pipelineSpecification.DebugName = "SoftBody Coupling Sphere Pipeline";
    m_RigidSphereMaterial = CreateRef<Material>(Pipeline::Create(pipelineSpecification));
    m_RigidSphereMaterial->SetUniform("u_Tint", math::Vec3{ 0.95f, 0.45f, 0.20f });
    m_RigidSphereMaterial->SetUniform("u_Alpha", 1.0f);

    // ========================================================================
    // Cloth Entity
    // ========================================================================
    // Clothを追加描画Passの特殊物として扱わず、通常のScene Entityへ統合します。
    // MeshRendererComponentが描画、MeshDeformationComponentが頂点更新を担当するため、
    // Scene側はSoftBodyClothDeformerという具体型を知る必要がありません。
    m_ClothEntity = scene->CreateEntity("XPBD Cloth Demo");

    TransformComponent& clothTransform = m_ClothEntity.GetComponent<TransformComponent>();
    clothTransform.Position = kClothWorldPosition;
    clothTransform.Scale = { kClothWorldScale, kClothWorldScale, kClothWorldScale };

    m_ClothEntity.AddComponent<MeshRendererComponent>(
        MeshRendererComponent{ m_ClothMesh, m_ClothMaterial });

    auto clothDeformer = CreateScope<SoftBodyClothDeformer>(kClothRows, kClothColumns);
    clothDeformer->SetCollisionSphere(
        kRigidSphereInitialLocalCenter,
        WorldToClothLocalLength(kRigidSphereWorldRadius));
    clothDeformer->SetCollisionPlane({ 0.0f, 1.0f, 0.0f }, kFloorLocalY);

    m_ClothDeformationInstance = CreateRef<MeshDeformationInstance>(
        m_ClothMesh,
        std::move(clothDeformer));

    m_ClothEntity.AddComponent<MeshDeformationComponent>(
        MeshDeformationComponent{ m_ClothDeformationInstance, true });

    // ========================================================================
    // Dynamic RigidBody Sphere Entity
    // ========================================================================
    // SphereもMeshRendererComponentを持つ通常Entityにします。
    // これで描画位置とPhysicsWorldが更新するTransformが同一データになります。
    m_RigidSphereEntity = scene->CreateEntity("SoftBody Coupling Rigid Sphere");

    TransformComponent& sphereTransform = m_RigidSphereEntity.GetComponent<TransformComponent>();
    sphereTransform.Position = ClothLocalToWorldPosition(kRigidSphereInitialLocalCenter);

    const float sphereDiameter = kRigidSphereWorldRadius * 2.0f;
    sphereTransform.Scale = { sphereDiameter, sphereDiameter, sphereDiameter };

    m_RigidSphereEntity.AddComponent<MeshRendererComponent>(
        MeshRendererComponent{ m_CollisionSphereMesh, m_RigidSphereMaterial });

    RigidBodyComponent rigidBody{};
    rigidBody.SetBodyType(BodyType::Dynamic);
    rigidBody.SetMass(kRigidSphereMass);
    rigidBody.LinearVelocity = kRigidSphereInitialVelocity;
    rigidBody.LinearDamping = 0.02f;
    rigidBody.AngularDamping = 0.04f;
    rigidBody.UseGravity = true;
    rigidBody.AllowSleep = false;
    m_RigidSphereEntity.AddComponent<RigidBodyComponent>(rigidBody);

    ColliderComponent collider{};
    collider.Type = ColliderType::Sphere;
    collider.Radius = kRigidSphereWorldRadius;
    collider.Restitution = 0.15f;
    collider.StaticFriction = 0.5f;
    collider.DynamicFriction = 0.35f;
    m_RigidSphereEntity.AddComponent<ColliderComponent>(collider);
}

void SoftBodyClothDemoLayer::OnDetach()
{
    Scene* scene = m_Application.GetScene();

    if (scene != nullptr)
    {
        if (static_cast<bool>(m_ClothEntity)
            && scene->IsEntityAlive(m_ClothEntity))
        {
            scene->DestroyEntity(m_ClothEntity);
        }

        if (static_cast<bool>(m_RigidSphereEntity)
            && scene->IsEntityAlive(m_RigidSphereEntity))
        {
            scene->DestroyEntity(m_RigidSphereEntity);
        }
    }

    m_ClothEntity = {};
    m_RigidSphereEntity = {};
    m_ClothDeformationInstance.reset();
    m_ClothMaterial.reset();
    m_RigidSphereMaterial.reset();
    m_CollisionSphereMesh.reset();
    m_ClothMesh.reset();
}

void SoftBodyClothDemoLayer::OnUpdate(float deltaTime)
{
    static_cast<void>(deltaTime);

    Scene* scene = m_Application.GetScene();
    if (scene == nullptr
        || m_ClothDeformationInstance == nullptr
        || static_cast<bool>(m_RigidSphereEntity) == false
        || scene->IsEntityAlive(m_RigidSphereEntity) == false)
    {
        return;
    }

    MeshDeformer* baseDeformer = m_ClothDeformationInstance->GetDeformer();
    SoftBodyClothDeformer* clothDeformer = dynamic_cast<SoftBodyClothDeformer*>(baseDeformer);
    if (clothDeformer == nullptr)
    {
        return;
    }

    ph::SoftBodySolver& solver = clothDeformer->GetSolver();
    const std::vector<ph::SoftBodySphereCollider>& sphereColliders = solver.GetSphereColliders();

    // ========================================================================
    // Soft Body -> Rigid Body reaction
    // ========================================================================
    // Cloth Constraintの反作用を通常RigidBodyへ返します。
    // 現在はSoft Particle質量系とRigidBody kg系が未統一なので、ScaleとClampを残しています。
    if (sphereColliders.empty() == false)
    {
        const ph::SoftBodySphereCollider& softSphere = sphereColliders.front();

        if (softSphere.ContactCount > 0u)
        {
            math::Vec3 worldReactionImpulse =
                softSphere.AccumulatedReactionImpulse
                * (kClothWorldScale * kSoftRigidReactionScale);

            worldReactionImpulse = ClampMagnitude(worldReactionImpulse, kMaxReactionImpulse);

            const math::Vec3 worldContactPoint =
                ClothLocalToWorldPosition(softSphere.GetAverageContactPoint());

            if (worldReactionImpulse.LengthSq() > math::Epsilon * math::Epsilon)
            {
                scene->GetPhysicsWorld().AddImpulseAtPoint(
                    *scene,
                    m_RigidSphereEntity,
                    worldReactionImpulse,
                    worldContactPoint);
            }
        }
    }

    // ========================================================================
    // Rigid Body -> Soft Body collider synchronization
    // ========================================================================
    // PhysicsWorldが更新したWorld Transformを次フレーム用ClothローカルColliderへ戻します。
    const TransformComponent& sphereTransform =
        m_RigidSphereEntity.GetComponent<TransformComponent>();
    const ColliderComponent& sphereCollider =
        m_RigidSphereEntity.GetComponent<ColliderComponent>();

    const math::Vec3 localSphereCenter =
        WorldToClothLocalPosition(sphereTransform.Position + sphereCollider.Offset);
    const float localSphereRadius = WorldToClothLocalLength(sphereCollider.Radius);

    clothDeformer->SetCollisionSphere(localSphereCenter, localSphereRadius);
}

void SoftBodyClothDemoLayer::OnRender()
{
    // 描画はSceneのMeshRendererComponent走査へ統合しました。
    // Layer側でRenderer::BeginScene()/EndScene()を追加実行するとGame ViewとScene Viewで
    // 描画経路が分岐するため、ここではSoftBody固有の追加Passを持ちません。
}

} // namespace Raven
