// Raven/Gltf/Debug/HumanSkinningDebugLayer.cpp
#include "Raven/Gltf/Debug/HumanSkinningDebugLayer.h"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#include "Raven/Gltf/GltfCoordinateSystem.h"
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
    std::vector<SpawnedSkinnedPrimitive>& primitives,
    std::string* errorMessage)
{
    constexpr float TargetHeight = 20.0f;
    constexpr float MinimumHeight = 1.0e-5f;

    const float maxFloat = std::numeric_limits<float>::max();
    math::Vec3 sourceBoundsMin{ maxFloat, maxFloat, maxFloat };
    math::Vec3 sourceBoundsMax{ -maxFloat, -maxFloat, -maxFloat };
    bool hasVertex = false;

    // ========================================================================
    // 1. glTF Node Transform適用後のRaven World AABBを計算
    // ========================================================================
    // Primitive頂点はMesh Local Spaceですが、SkinnedMeshSceneSpawnerがEntityへ設定した
    // TransformComponentにはglTF Node階層のGlobal Transformがすでに反映されています。
    // さらにScene配置境界でBuildGltfToRavenWorldTransform()も適用済みです。
    //
    // したがって、ここで得られるWorld座標は「glTF Node/Skin座標系を正規経路で解釈した結果」であり、
    // Debug LayerがGeometry形状から別のUp軸を推測して回転を追加する必要はありません。
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

            sourceBoundsMin.x = std::min(sourceBoundsMin.x, worldPosition.x);
            sourceBoundsMin.y = std::min(sourceBoundsMin.y, worldPosition.y);
            sourceBoundsMin.z = std::min(sourceBoundsMin.z, worldPosition.z);
            sourceBoundsMax.x = std::max(sourceBoundsMax.x, worldPosition.x);
            sourceBoundsMax.y = std::max(sourceBoundsMax.y, worldPosition.y);
            sourceBoundsMax.z = std::max(sourceBoundsMax.z, worldPosition.z);
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

    const math::Vec3 sourceBoundsCenter = (sourceBoundsMin + sourceBoundsMax) * 0.5f;
    const math::Vec3 sourceBoundsSize = sourceBoundsMax - sourceBoundsMin;

    // ========================================================================
    // 2. 高さ方向はglTF仕様の+Yを明示的に使用
    // ========================================================================
    // glTF 2.0ではAsset全体のUp軸は+Yで固定です。
    // Jointも通常Nodeと同じNode階層上にあり、Skeletonだけ別のUp軸を持つことはありません。
    // Blender等のZ-up Authoring Spaceから必要な変換はExporterがNode Transformへ符号化します。
    //
    // そのためAABBのX/Y/Zサイズ比較からUp軸を推測する旧実装は廃止します。
    // T-Poseの腕幅やAnimation Poseによって判定結果が変化することもなくなります。
    const float sourceHeight = sourceBoundsSize.y;
    if (sourceHeight <= MinimumHeight)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage =
                "glTF +Y upとしてHumanの高さを取得できませんでした。"
                "Node TransformまたはAsset Export設定を確認してください";
        }
        return false;
    }

    const float uniformScale = TargetHeight / sourceHeight;

    // ========================================================================
    // 3. Raven Worldの+Yを維持したままCenter / Floorだけ正規化
    // ========================================================================
    // Up軸回転は一切行いません。
    // X/Z中心を原点へ寄せ、Y最小値をFloor(Y=0)へ合わせるだけです。
    // これによりDebug表示処理は座標系変換ではなく「見やすい位置・サイズへの配置」だけを担当します。
    const math::Vec3 debugTranslation{
        -sourceBoundsCenter.x * uniformScale,
        -sourceBoundsMin.y * uniformScale,
        -sourceBoundsCenter.z * uniformScale
    };

    // ========================================================================
    // 4. Human全体へ同じUniform Scale / Translationを適用
    // ========================================================================
    // 元のWorld Rotationには触れません。
    // glTF Root NodeやArmature外側Nodeに含まれる基底変換を保持することが重要です。
    // Body / Clothes等のPrimitive間相対配置も、全Entityへ同一World変換を適用することで維持します。
    for (SpawnedSkinnedPrimitive& primitive : primitives)
    {
        if (static_cast<bool>(primitive.EntityHandle) == false
            || primitive.EntityHandle.HasComponent<TransformComponent>() == false)
        {
            continue;
        }

        TransformComponent& transform =
            primitive.EntityHandle.GetComponent<TransformComponent>();

        // 左から T_debug * S_uniform を掛けるのと同じWorld Position更新です。
        // RotationはglTF Node階層から得た値をそのまま維持します。
        transform.Position = debugTranslation + transform.Position * uniformScale;
        transform.Scale = transform.Scale * uniformScale;
    }

    std::cout
        << "[HumanSkinning] Debug Bounds:\n"
        << "  Min    = (" << sourceBoundsMin.x << ", " << sourceBoundsMin.y << ", " << sourceBoundsMin.z << ")\n"
        << "  Max    = (" << sourceBoundsMax.x << ", " << sourceBoundsMax.y << ", " << sourceBoundsMax.z << ")\n"
        << "  Center = (" << sourceBoundsCenter.x << ", " << sourceBoundsCenter.y << ", " << sourceBoundsCenter.z << ")\n"
        << "  Size   = (" << sourceBoundsSize.x << ", " << sourceBoundsSize.y << ", " << sourceBoundsSize.z << ")\n"
        << "  Coordinate System = " << GetGltfCoordinateSystemDescription() << '\n'
        << "  Debug Up = +Y (explicit, no AABB inference)\n"
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

    // 相対パスの場合、カレントワーキングディレクトリから解決されます
    std::filesystem::path resolvedPath = std::filesystem::absolute(m_ModelPath);
    
    if (std::filesystem::exists(resolvedPath) == false)
    {
        std::cout
            << "[HumanSkinning] " << m_ModelPath << "\n"
            << "  解決パス: " << resolvedPath << "\n"
            << "  カレントディレクトリ: " << std::filesystem::current_path() << "\n"
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

    // Debug表示正規化ではPrimitive EntityのTransformComponentを書き換えるため、
    // const参照ではなくSceneInstanceが所有する配列への非const参照が必要です。
    // GetPrimitives()にはconst/non-const overloadを用意し、Debug用途でもconst_castを使わず、
    // SceneInstance側の所有権境界を保ったまま明示的にmutableな配列を取得します。
    std::vector<SpawnedSkinnedPrimitive>& primitives = m_HumanInstance.GetPrimitives();
    if (primitives.empty())
    {
        std::cerr << "[HumanSkinning] Spawn後のPrimitiveが0件です。\n";
        DestroyHuman();
        return false;
    }

    // Human.glbの表示サイズ・位置だけをDebug用途に正規化します。
    // Up軸はglTF仕様(+Y)とNode/Skin Transformを正規データとして扱い、Geometry AABBから推測しません。
    // Skeleton / inverseBindMatrices / Node Rotationには一切追加補正を入れません。
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
