#include "Raven/Physics/SoftBody/Debug/SoftBodyJellyDemoLayer.h"

#include "Raven/Core/Application.h"
#include "Raven/Physics/SoftBody/SoftBodyJelly.h"
#include "Raven/Renderer/Material/Material.h"
#include "Raven/Renderer/Mesh/Deformation/MeshDeformationInstance.h"
#include "Raven/Renderer/Mesh/Deformation/SoftBodyJellyMeshDeformer.h"
#include "Raven/Renderer/Mesh/Mesh.h"
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
// XPBD Jelly Demo Parameters
// ============================================================================
// 3x3x3 Cellは4x4x4 = 64 Particleです。
// まずはVolume Constraintの変形を目視しやすく、Constraint数も過度に増えない規模にします。
constexpr uint32_t kJellyCellsX = 3u;
constexpr uint32_t kJellyCellsY = 3u;
constexpr uint32_t kJellyCellsZ = 3u;

// Jelly Solverは原点中心の小さなローカル直方体を扱い、World配置はEntity Transformへ分離します。
// Clothと同じく、SoftBody計算の数値スケールをScene Worldの大きさから独立させます。
constexpr math::Vec3 kJellyWorldPosition{ 18.0f, 12.0f, -6.0f };
constexpr float kJellyWorldScale = 10.0f;

// Scene床World Y=0をJellyローカルへ逆変換した値です。
// worldY = localY * scale + translationY なので localY = (0 - 12) / 10 になります。
constexpr float kJellyFloorLocalY = -12.0f / 10.0f;
}

void SoftBodyJellyDemoLayer::OnAttach()
{
    Scene* scene = m_Application.GetScene();
    if (scene == nullptr)
    {
        return;
    }

    // ========================================================================
    // Jelly Physics / Surface / Dynamic Mesh construction
    // ========================================================================
    // SoftBodyJellyMeshDeformer constructor内で
    //   Grid Particle
    //   Tetrahedron
    //   Distance Constraint
    //   Volume Constraint
    //   Surface Triangle
    // をまとめて構築します。
    // Scene Layer側は物理Topologyを再構築せず、描画用Dynamic Geometryだけ取得します。
    ph::SoftBodyJellySettings jellySettings{};
    jellySettings.CellsX = kJellyCellsX;
    jellySettings.CellsY = kJellyCellsY;
    jellySettings.CellsZ = kJellyCellsZ;
    jellySettings.Width = 1.0f;
    jellySettings.Height = 1.0f;
    jellySettings.Depth = 1.0f;
    jellySettings.InverseMass = 1.0f;
    jellySettings.DistanceCompliance = 0.0001f;
    jellySettings.VolumeCompliance = 0.000001f;
    jellySettings.VelocityDamping = 0.02f;

    auto jellyDeformer = CreateScope<SoftBodyJellyMeshDeformer>(
        jellySettings,
        math::Vec3{ 0.35f, 0.85f, 0.55f });

    // +Yを外側とするPlaneをScene床と同じWorld Y=0へ設定します。
    // Jellyが落下・変形しても床を抜けないことをClothと同じ条件で確認できます。
    jellyDeformer->SetCollisionPlane({ 0.0f, 1.0f, 0.0f }, kJellyFloorLocalY);

    Ref<MeshGeometry> jellyGeometry = jellyDeformer->CreateGeometry();
    if (jellyGeometry == nullptr)
    {
        return;
    }

    m_JellyMesh = CreateRef<Mesh>(jellyGeometry);
    if (m_JellyMesh == nullptr)
    {
        return;
    }

    ShaderLibrary shaderLibrary{};
    Ref<Shader> shader = shaderLibrary.Load(
        "SoftBodyJellyDemo",
        "Raven/Assets/Shaders/Vertex/test.vert",
        "Raven/Assets/Shaders/Fragment/test.frag");

    if (shader == nullptr)
    {
        m_JellyMesh.reset();
        return;
    }

    PipelineSpecification pipelineSpecification{};
    pipelineSpecification.DebugName = "SoftBody Jelly Demo Pipeline";
    pipelineSpecification.Shader = shader;
    pipelineSpecification.Topology = PrimitiveTopology::Triangles;
    pipelineSpecification.Cull = CullMode::None;
    pipelineSpecification.FrontFaceMode = FrontFace::CounterClockwise;
    pipelineSpecification.DepthTest = true;
    pipelineSpecification.DepthWrite = true;
    pipelineSpecification.DepthCompare = DepthCompareOperator::Less;
    pipelineSpecification.Blend = true;

    // Jellyは専用Materialを持たせます。
    // SceneGameの共通Materialとu_Tint状態を共有すると、Sphere等の描画時Uniform変更の影響を
    // 受けるため、SoftBody表示色は独立Materialで固定します。
    m_JellyMaterial = CreateRef<Material>(Pipeline::Create(pipelineSpecification));
    m_JellyMaterial->SetUniform("u_Tint", math::Vec3{ 0.35f, 0.85f, 0.55f });
    m_JellyMaterial->SetUniform("u_Alpha", 1.0f);

    // ========================================================================
    // Jelly Entity
    // ========================================================================
    // Jellyを特殊な描画物として扱わず、Clothと同じECS構成へ揃えます。
    m_JellyEntity = scene->CreateEntity("XPBD Jelly Demo");

    TransformComponent& jellyTransform = m_JellyEntity.GetComponent<TransformComponent>();
    jellyTransform.Position = kJellyWorldPosition;
    jellyTransform.Scale = { kJellyWorldScale, kJellyWorldScale, kJellyWorldScale };

    m_JellyEntity.AddComponent<MeshRendererComponent>(
        MeshRendererComponent{ m_JellyMesh, m_JellyMaterial });

    m_JellyDeformationInstance = CreateRef<MeshDeformationInstance>(
        m_JellyMesh,
        std::move(jellyDeformer));

    m_JellyEntity.AddComponent<MeshDeformationComponent>(
        MeshDeformationComponent{ m_JellyDeformationInstance, true });

    // 現在のSceneGame描画は移行途中でm_SpawnedEntitiesを利用しているため、
    // Game View / Scene Viewの双方へ出すために正式な登録APIを通します。
    // RenderSceneがECS View直接走査へ移行した後、この登録は不要になります。
    SceneGame* sceneGame = dynamic_cast<SceneGame*>(scene);
    if (sceneGame != nullptr)
    {
        sceneGame->RegisterRuntimeRenderEntity(m_JellyEntity);
    }
}

void SoftBodyJellyDemoLayer::OnDetach()
{
    Scene* scene = m_Application.GetScene();
    if (scene != nullptr)
    {
        SceneGame* sceneGame = dynamic_cast<SceneGame*>(scene);
        if (sceneGame != nullptr && static_cast<bool>(m_JellyEntity))
        {
            sceneGame->UnregisterRuntimeRenderEntity(m_JellyEntity);
        }

        if (static_cast<bool>(m_JellyEntity)
            && scene->IsEntityAlive(m_JellyEntity))
        {
            scene->DestroyEntity(m_JellyEntity);
        }
    }

    m_JellyEntity = {};
    m_JellyDeformationInstance.reset();
    m_JellyMaterial.reset();
    m_JellyMesh.reset();
}

void SoftBodyJellyDemoLayer::OnUpdate(float deltaTime)
{
    // Simulation更新はSceneGame::OnUpdateGame()からMeshDeformationSystemが一括実行します。
    // Layer側で二重StepするとConstraintの時間積分が2回進むため、ここでは更新しません。
    static_cast<void>(deltaTime);
}

void SoftBodyJellyDemoLayer::OnRender()
{
    // 描画はMeshRendererComponent経由でSceneへ統合しています。
    // Clothと同じくLayer独自のRenderer::BeginScene()/EndScene()は持ちません。
}

} // namespace Raven
