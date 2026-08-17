#include "Raven/Physics/SoftBody/Debug/SoftBodyClothDemoLayer.h"

#include "Raven/Core/Application.h"
#include "Raven/Renderer/Material/Material.h"
#include "Raven/Renderer/Mesh/Deformation/MeshDeformationInstance.h"
#include "Raven/Renderer/Mesh/Deformation/SoftBodyClothDeformer.h"
#include "Raven/Renderer/Mesh/PrimitiveMeshFactory.h"
#include "Raven/Renderer/Pipeline/Pipeline.h"
#include "Raven/Renderer/Renderer.h"
#include "Raven/Renderer/Shader/Shader.h"
#include "Raven/Scene/Components.h"
#include "Raven/Scene/Scene.h"
#include "Raven/Scene/SceneCameraSystem.h"

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

// Sphere ColliderはSolverのClothローカル空間で定義します。
// 描画時にも同じ値をWorldへ変換して使用することで、見えているSphereと当たり判定を一致させます。
constexpr math::Vec3 kCollisionSphereCenter{ 0.0f, -0.12f, 0.0f };
constexpr float kCollisionSphereRadius = 0.20f;

// Solverは[-0.5,+0.5]程度の小さなローカル座標で計算し、描画時だけWorldへ拡大・移動します。
// 物理計算を巨大なWorld座標から分離し、Constraint計算の数値スケールを安定させる狙いです。
constexpr math::Vec3 kClothWorldPosition{ 0.0f, 18.0f, -10.0f };
constexpr float kClothWorldScale = 22.0f;

// SceneGameの床はWorld Y=0です。
// SolverはClothローカル空間で動くため、worldY = localY * scale + translationY を逆変換し、
// localY = (0 - 18) / 22 として同じ床面をSoftBody Solverへ登録します。
constexpr float kFloorLocalY = -18.0f / 22.0f;
} // namespace

void SoftBodyClothDemoLayer::OnAttach()
{
    Scene* scene = m_Application.GetScene();
    if (scene == nullptr)
    {
        return;
    }

    // Clothは毎フレーム頂点が変化するためDynamic Gridを使用します。
    // SphereはCollision Constraintの位置を目視確認するための表示用Meshです。
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

    // ClothとCollision Sphereは同じ簡易Pipelineで描画します。
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

    m_ClothMaterial = CreateRef<Material>(Pipeline::Create(pipelineSpecification));
    m_ClothMaterial->SetUniform("u_Alpha", 1.0f);

    // Scene側にはSoftBody専用Componentを増やさず、既存MeshDeformationComponentだけを登録します。
    // SceneGame::OnUpdateGame()のMeshDeformationSystemが自動走査するため、SoftBody固有の
    // Update呼び出しをSceneGameへ埋め込まずに済みます。
    m_ClothEntity = scene->CreateEntity("XPBD Cloth Demo");

    auto clothDeformer = CreateScope<SoftBodyClothDeformer>(kClothRows, kClothColumns);
    clothDeformer->SetCollisionSphere(kCollisionSphereCenter, kCollisionSphereRadius);

    // +Yを外側とするPlaneをScene床と同じWorld Y=0へ配置します。
    // ClothがSphereを滑り落ちた後も床より下へ抜けないことを目視確認できます。
    clothDeformer->SetCollisionPlane({ 0.0f, 1.0f, 0.0f }, kFloorLocalY);

    auto deformationInstance = std::make_shared<MeshDeformationInstance>(
        m_ClothMesh,
        std::move(clothDeformer));

    m_ClothEntity.AddComponent<MeshDeformationComponent>(
        MeshDeformationComponent{ std::move(deformationInstance), true });
}

void SoftBodyClothDemoLayer::OnDetach()
{
    Scene* scene = m_Application.GetScene();

    // Layerだけが破棄されるケースでもScene内にデモEntityを残さないよう明示的に破棄します。
    if (scene != nullptr
        && static_cast<bool>(m_ClothEntity)
        && scene->IsEntityAlive(m_ClothEntity))
    {
        scene->DestroyEntity(m_ClothEntity);
    }

    m_ClothEntity = {};
    m_ClothMaterial.reset();
    m_CollisionSphereMesh.reset();
    m_ClothMesh.reset();
}

void SoftBodyClothDemoLayer::OnRender()
{
    if (m_ClothMesh == nullptr
        || m_CollisionSphereMesh == nullptr
        || m_ClothMaterial == nullptr)
    {
        return;
    }

    Scene* scene = m_Application.GetScene();
    if (scene == nullptr)
    {
        return;
    }

    // 既存Sceneと同じRuntime Cameraを使用し、SoftBodyだけ別視点になることを避けます。
    const Window& window = m_Application.GetWindow();
    SceneCamera* camera = SceneCameraSystem::UpdatePrimaryCamera(
        *scene,
        static_cast<float>(window.GetWidth()),
        static_cast<float>(window.GetHeight()));

    if (camera == nullptr)
    {
        return;
    }

    // SoftBody SolverはClothローカル空間[-0.5,+0.5]付近で解いています。
    // 描画時だけWorldへ拡大・移動することで、Solver内部の数値スケールを小さく保ちます。
    math::Mat4 clothTransform = math::Mat4::Identity();
    clothTransform = math::Translate(clothTransform, kClothWorldPosition);
    clothTransform = math::Scale(
        clothTransform,
        { kClothWorldScale, kClothWorldScale, kClothWorldScale });

    // Collision SphereもSolverローカル座標から同じTransform規則でWorldへ変換します。
    const math::Vec3 sphereWorldCenter =
        kClothWorldPosition + kCollisionSphereCenter * kClothWorldScale;

    // PrimitiveMeshFactory::CreateSphere()は半径0.5なので、直径2RをScaleへ設定します。
    const float sphereWorldDiameter =
        kCollisionSphereRadius * 2.0f * kClothWorldScale;

    math::Mat4 sphereTransform = math::Mat4::Identity();
    sphereTransform = math::Translate(sphereTransform, sphereWorldCenter);
    sphereTransform = math::Scale(
        sphereTransform,
        { sphereWorldDiameter, sphereWorldDiameter, sphereWorldDiameter });

    // SceneGame本体のColor/Depthを保持したまま追加Passとして描画します。
    // Clearを行わないため、既存Sceneの上へSphereとClothだけを重ねます。
    Renderer::BeginScene(*camera);

    // Collider位置を識別しやすいようSphereをオレンジ、Clothを青で描画します。
    m_ClothMaterial->SetUniform("u_Tint", math::Vec3{ 0.95f, 0.45f, 0.20f });
    Renderer::Draw(m_CollisionSphereMesh, m_ClothMaterial, sphereTransform);

    m_ClothMaterial->SetUniform("u_Tint", math::Vec3{ 0.35f, 0.65f, 1.0f });
    Renderer::Draw(m_ClothMesh, m_ClothMaterial, clothTransform);

    Renderer::EndScene();
}

} // namespace Raven
