// Raven/Gltf/Debug/HumanSkinningDebugLayer.cpp
#include "Raven/Gltf/Debug/HumanSkinningDebugLayer.h"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#include "Raven/Renderer/Mesh/Mesh.h"
#include "Raven/Renderer/Mesh/MeshGeometry.h"
#include "Raven/Scene/Components.h"
#include "Raven/Scene/SceneGame.h"

namespace Raven
{
namespace Gltf
{
namespace
{

math::Vec3 TransformPosition(const math::Mat4& matrix, const math::Vec3& position)
{
    const math::Vec4 transformed = matrix * math::Vec4{ position.x, position.y, position.z, 1.0f };
    return { transformed.x, transformed.y, transformed.z };
}

bool NormalizeHumanForDebugView(
    const std::vector<SpawnedSkinnedPrimitive>& primitives,
    std::string* errorMessage)
{
    constexpr float TargetHeight = 20.0f;
    constexpr float MinimumHeight = 1.0e-5f;

    // Debug表示ではHumanの足元をおおむねFloor(Y=0)へ合わせたいので、
    // 高さ20へ正規化した後のBounds中心をY=10へ配置します。
    // X/Zは原点中心にすることで、既定Camera (0,40,80) -> Origin から見つけやすくします。
    const math::Vec3 targetCenter{ 0.0f, TargetHeight * 0.5f, 0.0f };

    const float maxFloat = std::numeric_limits<float>::max();
    math::Vec3 boundsMin{ maxFloat, maxFloat, maxFloat };
    math::Vec3 boundsMax{ -maxFloat, -maxFloat, -maxFloat };
    bool hasVertex = false;

    // ========================================================================
    // 全Primitiveを同じWorld Spaceへ変換してAABBを計算
    // ========================================================================
    // Primitiveごとの頂点はMesh Local Spaceにあります。
    // Human.glbでは複数Primitiveが別Node Transformを持つ可能性があるため、Local座標を
    // そのまま比較せず、現在のEntity TransformでWorld Spaceへ変換してから統合します。
    for (const SpawnedSkinnedPrimitive& primitive : primitives)
    {
        if (static_cast<bool>(primitive.EntityHandle) == false
            || primitive.EntityHandle.HasComponent<TransformComponent>() == false
            || primitive.EntityHandle.HasComponent<MeshRendererComponent>() == false)
        {
            continue;
        }

        const TransformComponent& transform =
            primitive.EntityHandle.GetComponent<TransformComponent>();
        const MeshRendererComponent& meshRenderer =
            primitive.EntityHandle.GetComponent<MeshRendererComponent>();

        if (meshRenderer.Mesh == nullptr
            || meshRenderer.Mesh->GetGeometry() == nullptr)
        {
            continue;
        }

        const math::Mat4 worldTransform = transform.GetTransform();
        const std::vector<MeshVertex>& vertices = meshRenderer.Mesh->GetGeometry()->GetVertices();

        for (const MeshVertex& vertex : vertices)
        {
            const math::Vec3 worldPosition = TransformPosition(worldTransform, vertex.Position);

            boundsMin.x = std::min(boundsMin.x, worldPosition.x);
            boundsMin.y = std::min(boundsMin.y, worldPosition.y);
            boundsMin.z = std::min(boundsMin.z, worldPosition.z);
            boundsMax.x = std::max(boundsMax.x, worldPosition.x);
            boundsMax.y = std::max(boundsMax.y, worldPosition.y);
            boundsMax.z = std::max(boundsMax.z, worldPosition.z);
            hasVertex = true;
        }
    }

    if (hasVertex == false)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "Human Debug表示用Boundsを計算できませんでした";
        }
        return false;
    }

    const math::Vec3 boundsCenter = (boundsMin + boundsMax) * 0.5f;
    const math::Vec3 boundsSize = boundsMax - boundsMin;
    const float height = boundsSize.y;

    if (height <= MinimumHeight)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "Human Debug表示用Boundsの高さが0に近すぎます";
        }
        return false;
    }

    const float uniformScale = TargetHeight / height;

    // ========================================================================
    // Human全体へ共通のDebug表示Transformを左から適用
    // ========================================================================
    // 各Primitiveは既にglTF World TransformをTRSとして持っています。
    // ここで個別に原点へ移動するとBody/Clothes等の相対配置を壊すため、全Primitiveへ
    //
    //   p' = TargetCenter + UniformScale * (p - BoundsCenter)
    //
    // という同じWorld Space変換を適用します。
    // Uniform Scaleなので既存Rotationとは可換であり、PositionとScaleだけを更新すれば
    // glTF由来のPrimitive間相対配置とSkinning Mesh Local Spaceは維持されます。
    for (const SpawnedSkinnedPrimitive& primitive : primitives)
    {
        if (static_cast<bool>(primitive.EntityHandle) == false
            || primitive.EntityHandle.HasComponent<TransformComponent>() == false)
        {
            continue;
        }

        TransformComponent& transform =
            primitive.EntityHandle.GetComponent<TransformComponent>();

        transform.Position =
            targetCenter
            + (transform.Position - boundsCenter) * uniformScale;
        transform.Scale = transform.Scale * uniformScale;
    }

    std::cout
        << "[HumanSkinning] Debug Bounds:\n"
        << "  Min    = (" << boundsMin.x << ", " << boundsMin.y << ", " << boundsMin.z << ")\n"
        << "  Max    = (" << boundsMax.x << ", " << boundsMax.y << ", " << boundsMax.z << ")\n"
        << "  Center = (" << boundsCenter.x << ", " << boundsCenter.y << ", " << boundsCenter.z << ")\n"
        << "  Size   = (" << boundsSize.x << ", " << boundsSize.y << ", " << boundsSize.z << ")\n"
        << "  Debug Scale = " << uniformScale << '\n';

    return true;
}

} // namespace

HumanSkinningDebugLayer::~HumanSkinningDebugLayer() = default;

bool HumanSkinningDebugLayer::TryInitialize()
{
    if (m_Initialized)
    {
        return true;
    }

    if (m_InitializationAttempted)
    {
        return false;
    }

    // LayerはSceneGame constructorから登録されますが、Human Mesh/Material生成はOnCreate後の
    // 最初のUpdateまで遅延します。これによりOpenGL/Pipeline初期化順へ依存しません。
    if (m_Scene.m_Material == nullptr)
    {
        return false;
    }

    m_InitializationAttempted = true;

    if (std::filesystem::exists(m_ModelPath) == false)
    {
        std::cout
            << "[HumanSkinning] " << m_ModelPath
            << " が見つからないためHuman検証をskipします。\n";
        return false;
    }

    std::string errorMessage;
    if (SkinnedMeshSceneSpawner::SpawnFromGlb(
            m_Scene,
            m_ModelPath,
            m_Scene.m_Material,
            m_HumanInstance,
            &errorMessage) == false)
    {
        std::cerr
            << "[HumanSkinning] Human.glbのScene配置に失敗しました: "
            << errorMessage << '\n';
        return false;
    }

    const std::vector<SpawnedSkinnedPrimitive>& primitives = m_HumanInstance.GetPrimitives();
    if (primitives.empty())
    {
        std::cerr << "[HumanSkinning] Spawn後のPrimitiveが0件です。\n";
        DestroyHuman();
        return false;
    }

    // Human.glb固有の単位・Scene Node位置に依存せず手動Skinning確認できるよう、
    // 全Primitiveの現在Bind Pose頂点からWorld AABBを求めてDebug表示用に正規化します。
    // Skinning自体はMesh Local Spaceで完結しているため、ここではScene Entity Transformだけを
    // 共通変換し、Skeleton / inverseBindMatricesには一切手を加えません。
    if (NormalizeHumanForDebugView(primitives, &errorMessage) == false)
    {
        std::cerr
            << "[HumanSkinning] Human.glbのDebug表示正規化に失敗しました: "
            << errorMessage << '\n';
        DestroyHuman();
        return false;
    }

    const std::size_t skinIndex = primitives.front().SkinIndex;
    if (m_Controller.Initialize(m_HumanInstance, skinIndex, &errorMessage) == false)
    {
        std::cerr
            << "[HumanSkinning] Debug Controllerの初期化に失敗しました: "
            << errorMessage << '\n';
        DestroyHuman();
        return false;
    }

    // SceneGame::RenderScene()は現段階ではECS全体ではなくm_SpawnedEntitiesを描画対象にしています。
    // Spawnerが生成したHuman Entityも同じ既存描画経路へ流すため、ここでHandleを登録します。
    // LifetimeはSceneGame::OnDestroy()の既存Entity破棄ループへ統一します。
    for (const SpawnedSkinnedPrimitive& primitive : primitives)
    {
        if (static_cast<bool>(primitive.EntityHandle) == false)
        {
            continue;
        }

        m_Scene.m_SpawnedEntities.emplace_back(primitive.EntityHandle);
    }

    std::cout << "[HumanSkinning] Human.glbを読み込みました。Bone一覧:\n";
    for (const std::string& boneName : m_Controller.GetBoneNames())
    {
        std::cout << "  - " << boneName << '\n';
    }

    std::cout
        << "[HumanSkinning] Controls: U/I LeftUpperArm, J/K LeftForeArm, M/L Head, R Reset\n"
        << "[HumanSkinning] Resolved LeftUpperArm: "
        << (m_Controller.GetLeftUpperArmBoneName().empty()
            ? "<not found>"
            : m_Controller.GetLeftUpperArmBoneName())
        << '\n'
        << "[HumanSkinning] Resolved LeftForeArm: "
        << (m_Controller.GetLeftForeArmBoneName().empty()
            ? "<not found>"
            : m_Controller.GetLeftForeArmBoneName())
        << '\n'
        << "[HumanSkinning] Resolved Head: "
        << (m_Controller.GetHeadBoneName().empty()
            ? "<not found>"
            : m_Controller.GetHeadBoneName())
        << '\n';

    m_Initialized = true;
    return true;
}

void HumanSkinningDebugLayer::OnUpdate(float deltaTime)
{
    // SceneGameでは現在、Layer UpdateがOnUpdateGame()とScene::OnUpdateLayer()の2経路から
    // 呼ばれます。Human debugだけ二重入力しないよう、1回目だけ処理しOnRender()で解除します。
    if (m_UpdatedSinceRender)
    {
        return;
    }
    m_UpdatedSinceRender = true;

    if (m_Initialized == false)
    {
        TryInitialize();
    }

    if (m_Initialized == false)
    {
        return;
    }

    std::string errorMessage;
    if (m_Controller.Update(deltaTime, &errorMessage) == false)
    {
        std::cerr
            << "[HumanSkinning] Bone手動操作に失敗しました: "
            << errorMessage << '\n';
    }
}

void HumanSkinningDebugLayer::OnRender()
{
    // 次のUpdate frameで1回だけ入力処理できるよう解除します。
    m_UpdatedSinceRender = false;
}

void HumanSkinningDebugLayer::DestroyHuman()
{
    // 初期化途中で失敗した場合、まだSceneGame::m_SpawnedEntitiesへ登録していないEntityを
    // Spawner経由で確実に破棄します。成功後の通常LifetimeはSceneGame::OnDestroy()へ統一します。
    if (m_HumanInstance.IsValid())
    {
        SkinnedMeshSceneSpawner::Destroy(m_Scene, m_HumanInstance);
    }

    m_Controller.Reset();
    m_HumanInstance = {};
    m_Initialized = false;
}

} // namespace Gltf
} // namespace Raven
