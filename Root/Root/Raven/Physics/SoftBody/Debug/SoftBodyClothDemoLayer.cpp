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
#include "Raven/Scene/SceneGame.h"

namespace Raven
{
namespace
{
// ============================================================================
// XPBD Cloth Demo Parameters
// ============================================================================
// 24x24セル = 25x25 Particleです。
// 最初の検証として十分に滑らかでありながら、Constraint数もまだ扱いやすい規模にしています。
constexpr uint32_t kClothRows = 24u;
constexpr uint32_t kClothColumns = 24u;

// Solverは[-0.5,+0.5]程度の小さなローカル座標で計算し、Entity TransformだけでWorldへ拡大・移動します。
// 物理計算を巨大なWorld座標から分離し、Constraint計算の数値スケールを安定させる狙いです。
// 描画をSceneへ統合した現在も、Solver座標系とWorld座標系を分離する設計自体は変わりません。
constexpr math::Vec3 kClothWorldPosition{ 0.0f, 18.0f, -10.0f };
constexpr float kClothWorldScale = 22.0f;

// SceneGameの床はWorld Y=0です。
// SolverはClothローカル空間で動くため、worldY = localY * scale + translationY を逆変換し、
// localY = (0 - 18) / 22 として同じ床面をSoftBody Solverへ登録します。
constexpr float kFloorLocalY = -18.0f / 22.0f;

// ============================================================================
// Dynamic RigidBody Sphere Parameters
// ============================================================================
// SphereはCloth正面(+Z側)から-Z方向へ飛ばし、垂直に吊ったClothへ衝突させます。
// 初期位置もClothローカル座標で記述し、World変換規則をClothと共有します。
constexpr math::Vec3 kRigidSphereInitialLocalCenter{ 0.0f, 0.0f, 0.36f };
constexpr float kRigidSphereWorldRadius = 2.4f;
constexpr float kRigidSphereMass = 4.0f;
constexpr math::Vec3 kRigidSphereInitialVelocity{ 0.0f, 0.0f, -8.0f };

// XPBD Sphere Collisionは現在DeltaLambda / dtから反作用Impulseを計算します。
// 以前のPosition Correction由来の推定より反復回数への依存は小さくなりましたが、SoftBody Particleの
// mass scaleとRigidBodyのkgはまだ共通単位系として校正していません。そのためデモ段階では
// Reaction ScaleとClampを残し、異なる質量系を接続した際の過大反作用を防ぎます。
// 将来Rigid/Soft共通Constraint Solverへ統合した段階で、この経験的Scaleを除去する想定です。
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

    // Clothは毎フレーム頂点が変化するためDynamic Gridを使用します。
    // Sphere MeshはSoft/Rigid連成対象のDynamic RigidBodyを通常Scene描画で可視化するために使用します。
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

    // ClothとRigidBody Sphereは同じShader/Pipeline設定を利用します。
    // Clothは表裏の両方を確認したいためCullMode::Noneにしています。
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
    // Scene側にはSoftBody専用描画処理を増やさず、既存の
    // MeshRendererComponent + MeshDeformationComponentとして登録します。
    // MeshDeformationSystemがComponentを自動走査するため、SoftBody固有の頂点Update呼び出しを
    // SceneGameへ埋め込まずに済みます。また描画も通常MeshRenderer経路へ統合できます。
    m_ClothEntity = scene->CreateEntity("XPBD Cloth Demo");

    // Solver内部はローカル座標のまま維持し、Worldへの配置はTransformComponentだけで表現します。
    // これによりMeshDeformerはScene上の配置位置を知る必要がありません。
    TransformComponent& clothTransform = m_ClothEntity.GetComponent<TransformComponent>();
    clothTransform.Position = kClothWorldPosition;
    clothTransform.Scale = { kClothWorldScale, kClothWorldScale, kClothWorldScale };

    m_ClothEntity.AddComponent<MeshRendererComponent>(
        MeshRendererComponent{ m_ClothMesh, m_ClothMaterial });

    auto clothDeformer = CreateScope<SoftBodyClothDeformer>(kClothRows, kClothColumns);

    // 最初のSphere Collider位置は、この後生成するRigidBody Sphereの初期Transformと一致させます。
    clothDeformer->SetCollisionSphere(
        kRigidSphereInitialLocalCenter,
        WorldToClothLocalLength(kRigidSphereWorldRadius));

    // +Yを外側とするPlaneをScene床と同じWorld Y=0へ配置します。
    // Clothが衝突後に落下しても床より下へ抜けないことを目視確認できます。
    clothDeformer->SetCollisionPlane({ 0.0f, 1.0f, 0.0f }, kFloorLocalY);

    m_ClothDeformationInstance = CreateRef<MeshDeformationInstance>(
        m_ClothMesh,
        std::move(clothDeformer));

    m_ClothEntity.AddComponent<MeshDeformationComponent>(
        MeshDeformationComponent{ m_ClothDeformationInstance, true });

    // ========================================================================
    // Dynamic RigidBody Sphere Entity
    // ========================================================================
    // このSphereは表示専用ではなく、通常のPhysicsWorldへ参加するDynamic Bodyです。
    // 現在はMeshRendererComponentも同じEntityへ登録しているため、PhysicsWorldが更新するTransformを
    // Scene Rendererも直接参照します。表示用位置をLayer側へ複製せず、床や既存RigidBodyとの衝突と
    // 描画を同じECS Entityから処理できることが今回のScene統合で重要な点です。
    m_RigidSphereEntity = scene->CreateEntity("SoftBody Coupling Rigid Sphere");

    TransformComponent& sphereTransform = m_RigidSphereEntity.GetComponent<TransformComponent>();
    sphereTransform.Position = ClothLocalToWorldPosition(kRigidSphereInitialLocalCenter);

    // PrimitiveMeshFactory::CreateSphere()は半径0.5なので、直径2RをScaleへ設定します。
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

    // 連成確認中に微小速度でSleepすると反作用の検証が分かりにくいため、デモSphereはSleepを無効化します。
    rigidBody.AllowSleep = false;
    m_RigidSphereEntity.AddComponent<RigidBodyComponent>(rigidBody);

    ColliderComponent collider{};
    collider.Type = ColliderType::Sphere;
    collider.Radius = kRigidSphereWorldRadius;
    collider.Restitution = 0.15f;
    collider.StaticFriction = 0.5f;
    collider.DynamicFriction = 0.35f;
    m_RigidSphereEntity.AddComponent<ColliderComponent>(collider);

    // 現在SceneGame::RenderScene()は移行途中でm_SpawnedEntitiesを描画対象一覧としているため、
    // Application Layerが生成したCloth/SphereもGame View / Scene Viewへ表示できるよう正式APIから登録します。
    // RenderSceneがECS View直接走査へ移行した後、この明示登録は不要になります。
    SceneGame* sceneGame = dynamic_cast<SceneGame*>(scene);
    if (sceneGame != nullptr)
    {
        sceneGame->RegisterRuntimeRenderEntity(m_ClothEntity);
        sceneGame->RegisterRuntimeRenderEntity(m_RigidSphereEntity);
    }
}

void SoftBodyClothDemoLayer::OnDetach()
{
    Scene* scene = m_Application.GetScene();

    // Layerだけが破棄されるケースでもScene内にデモEntityを残さないよう明示的に破棄します。
    if (scene != nullptr)
    {
        SceneGame* sceneGame = dynamic_cast<SceneGame*>(scene);
        if (sceneGame != nullptr)
        {
            if (static_cast<bool>(m_ClothEntity))
            {
                sceneGame->UnregisterRuntimeRenderEntity(m_ClothEntity);
            }

            if (static_cast<bool>(m_RigidSphereEntity))
            {
                sceneGame->UnregisterRuntimeRenderEntity(m_RigidSphereEntity);
            }
        }

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
    // Scene更新内ではCloth StepがRigidBody固定Stepより前に走ります。
    // Application LayerであるこのOnUpdate()はScene更新後に呼ばれるため、ここで直前Cloth Stepの
    // FeedbackをRigidBody速度へ即時反映し、次のPhysics固定Stepから運動へ参加させます。
    if (sphereColliders.empty() == false)
    {
        const ph::SoftBodySphereCollider& softSphere = sphereColliders.front();

        if (softSphere.ContactCount > 0u)
        {
            // AccumulatedReactionImpulseはXPBD Sphere ConstraintのDeltaLambda / dtから求めた
            // Clothローカル単位のImpulse相当です。uniform scaleを掛けてWorld長さ単位へ変換した後、
            // Particle質量系とRigidBody kg系の未校正差だけをReaction Scaleで抑えます。
            math::Vec3 worldReactionImpulse =
                softSphere.AccumulatedReactionImpulse
                * (kClothWorldScale * kSoftRigidReactionScale);

            worldReactionImpulse = ClampMagnitude(worldReactionImpulse, kMaxReactionImpulse);

            const math::Vec3 worldContactPoint =
                ClothLocalToWorldPosition(softSphere.GetAverageContactPoint());

            if (worldReactionImpulse.LengthSq() > math::Epsilon * math::Epsilon)
            {
                // 接触点へImpulseを返すことで、中心を外れたCloth接触ではRigidBodyのAngularVelocityにも
                // r x J が反映されます。これによりSoft/Rigid連成の回転反作用も最小構成で確認できます。
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
    // SceneのPhysics Stepを終えた最新TransformをClothローカルColliderへ変換します。
    // この値は次フレームのMeshDeformationSystem::Update()で使用されるため、RigidBody Sphereが
    // 移動してもCloth側Collision Sphereが追従します。
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
    // ========================================================================
    // Rendering is intentionally owned by Scene
    // ========================================================================
    // 旧実装ではここでRuntime Cameraを再取得し、Renderer::BeginScene()/EndScene()による
    // Cloth専用の追加描画Passを実行していました。
    //
    // 現在はClothとRigidBody Sphereの両方がMeshRendererComponentを持つ通常Entityです。
    // そのため描画はSceneの共通MeshRenderer経路へ一本化し、このLayerでは追加描画を行いません。
    // これによりGame View / Scene ViewでSoftBodyだけ描画経路が分岐する問題を避け、
    // Camera Context・Depth・Material・Entity Transformも他のScene Objectと同じ規則で扱えます。
}

} // namespace Raven
