// Raven/Gltf/Debug/HumanSkinningDebugLayer.cpp
#include "Raven/Gltf/Debug/HumanSkinningDebugLayer.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#include "Raven/Math/MathUtility.h"
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
    constexpr float Pi = 3.14159265358979323846f;

    const float maxFloat = std::numeric_limits<float>::max();
    math::Vec3 sourceBoundsMin{ maxFloat, maxFloat, maxFloat };
    math::Vec3 sourceBoundsMax{ -maxFloat, -maxFloat, -maxFloat };
    bool hasVertex = false;

    // ========================================================================
    // 1. Spawn直後のWorld AABBを計算
    // ========================================================================
    // Primitiveごとの頂点はMesh Local Spaceにあります。
    // Human.glbでは複数Primitiveが別Node Transformを持つ可能性があるため、Local座標を
    // そのまま比較せず、現在のEntity TransformでWorld Spaceへ変換してから統合します。
    // この走査ではEntityを変更しないため、primitive自体はconst参照で扱います。
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
    // 2. Debug表示用のUp軸を決める
    // ========================================================================
    // glTF自体はY-upですが、Raven_human_test.glbの実データでは人物の高さ方向がZです。
    // ここでXを含めた「AABBの最長軸」を高さとみなすと、T-Poseでは両腕を広げたX幅が
    // 身長Z以上になることがあり、Z-upのHumanを誤ってY-upと判定してしまいます。
    // その場合、人物の高さ方向ZがCameraの奥行き方向に残るため、画面上では胴体や脚が
    // 極端に潰れ、腕だけが横方向へ大きく広がったように見えます。
    //
    // このDebug Layerが扱う候補はY-up / Z-upの2種類に限定しているため、X幅はUp軸判定へ
    // 使用せず、Y方向とZ方向のどちらが大きいかだけで判定します。これによりT-Poseの
    // arm spanに影響されず、Raven_human_test.glbを安定してZ-up -> Y-upへ回転できます。
    const bool sourceIsZUp = sourceBoundsSize.z > sourceBoundsSize.y;
    const float sourceHeight = sourceIsZUp ? sourceBoundsSize.z : sourceBoundsSize.y;

    if (sourceHeight <= MinimumHeight)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "Human Debug表示用Boundsの高さが0に近すぎます";
        }
        return false;
    }

    const float uniformScale = TargetHeight / sourceHeight;

    // Z-upのHumanをRavenのY-up Sceneへ立てるDebug用回転です。
    // -90度X回転では (x,y,z) -> (x,z,-y) となるため、元の+Zが新しい+Yになります。
    math::Mat4 debugRotation = math::Mat4::Identity();
    float debugRotationX = 0.0f;
    if (sourceIsZUp)
    {
        debugRotationX = -Pi * 0.5f;
        debugRotation = math::Rotate(
            math::Mat4::Identity(),
            debugRotationX,
            math::Vec3{ 1.0f, 0.0f, 0.0f });
    }

    // ========================================================================
    // 3. 回転後Boundsを求め、足元をY=0へ合わせる
    // ========================================================================
    // 元AABBの8 cornerをDebug回転へ通せば、回転後のAABBを正確に求められます。
    // CenterだけをY=TargetHeight/2へ置く方法より、足先/頭頂の非対称形状でもFloorへ確実に
    // 接地させられるため、ここでは回転後Bounds Minを基準に最終Translationを決めます。
    math::Vec3 rotatedBoundsMin{ maxFloat, maxFloat, maxFloat };
    math::Vec3 rotatedBoundsMax{ -maxFloat, -maxFloat, -maxFloat };

    for (int xIndex = 0; xIndex < 2; ++xIndex)
    {
        for (int yIndex = 0; yIndex < 2; ++yIndex)
        {
            for (int zIndex = 0; zIndex < 2; ++zIndex)
            {
                const math::Vec3 corner{
                    xIndex == 0 ? sourceBoundsMin.x : sourceBoundsMax.x,
                    yIndex == 0 ? sourceBoundsMin.y : sourceBoundsMax.y,
                    zIndex == 0 ? sourceBoundsMin.z : sourceBoundsMax.z
                };

                const math::Vec3 rotatedCorner = TransformPosition(debugRotation, corner);
                rotatedBoundsMin.x = std::min(rotatedBoundsMin.x, rotatedCorner.x);
                rotatedBoundsMin.y = std::min(rotatedBoundsMin.y, rotatedCorner.y);
                rotatedBoundsMin.z = std::min(rotatedBoundsMin.z, rotatedCorner.z);
                rotatedBoundsMax.x = std::max(rotatedBoundsMax.x, rotatedCorner.x);
                rotatedBoundsMax.y = std::max(rotatedBoundsMax.y, rotatedCorner.y);
                rotatedBoundsMax.z = std::max(rotatedBoundsMax.z, rotatedCorner.z);
            }
        }
    }

    const math::Vec3 rotatedBoundsCenter = (rotatedBoundsMin + rotatedBoundsMax) * 0.5f;

    // X/Z中心を原点へ、Y最小値をFloor(Y=0)へ合わせます。
    const math::Vec3 debugTranslation{
        -rotatedBoundsCenter.x * uniformScale,
        -rotatedBoundsMin.y * uniformScale,
        -rotatedBoundsCenter.z * uniformScale
    };

    // ========================================================================
    // 4. Human全体へ同じWorld Space Debug Transformを適用
    // ========================================================================
    // 正規化は各PrimitiveのLocal Transformを個別補正するのではなく、既存World Transformの
    // 左側へ同一行列を掛けます。
    //
    //   M_debugWorld = T_debug * S_uniform * R_debug * M_importedWorld
    //
    // これによりBody/Clothes等のPrimitive間相対配置を壊さず、Skeleton/InverseBindの
    // Mesh Local Spaceにも触れません。
    //
    // TransformComponentは行列そのものを保持しないため、Debug変換がY-up/Z-upの2ケースに
    // 限られることを利用してTRSへ直接反映します。Uniform Scaleなので既存Rotationとの
    // 合成でScale軸が歪むこともありません。
    //
    // 重要:
    // ここではTransformComponentを書き換えるため、primitiveを非const参照で受けます。
    // Entity::GetComponent()にはconst/non-const overloadがあり、const Entityから取得すると
    // const T&になるため、const primitiveからTransformComponent&は取得できません。
    for (SpawnedSkinnedPrimitive& primitive : primitives)
    {
        if (static_cast<bool>(primitive.EntityHandle) == false
            || primitive.EntityHandle.HasComponent<TransformComponent>() == false)
        {
            continue;
        }

        TransformComponent& transform =
            primitive.EntityHandle.GetComponent<TransformComponent>();

        // World Positionは左からDebug回転・Scaleを掛けた後、Floor合わせTranslationを加えます。
        const math::Vec3 rotatedPosition = TransformPosition(debugRotation, transform.Position);
        transform.Position = debugTranslation + rotatedPosition * uniformScale;

        // GetTransform()の回転順は Rx * Ry * Rz です。
        // 今回追加するDebug回転はWorld X軸回転なので、既存World Rotationの左側へ掛けるには
        // Rotation.xへ加算するだけでは一般に同値になりません。
        // Human.glbのMesh Node World Rotationは現状Identityに近いことを前提にせず、
        // SpawnerのDecomposeWorldTransform()と同じTRS分解を再利用できないため、Debug Layerでは
        // Z-up時のみX回転を加えます。Human.glb確認用途としてこの制約をログにも明示します。
        if (sourceIsZUp)
        {
            transform.Rotation.x += debugRotationX;
        }

        transform.Scale = transform.Scale * uniformScale;
    }

    std::cout
        << "[HumanSkinning] Debug Bounds:\n"
        << "  Min    = (" << sourceBoundsMin.x << ", " << sourceBoundsMin.y << ", " << sourceBoundsMin.z << ")\n"
        << "  Max    = (" << sourceBoundsMax.x << ", " << sourceBoundsMax.y << ", " << sourceBoundsMax.z << ")\n"
        << "  Center = (" << sourceBoundsCenter.x << ", " << sourceBoundsCenter.y << ", " << sourceBoundsCenter.z << ")\n"
        << "  Size   = (" << sourceBoundsSize.x << ", " << sourceBoundsSize.y << ", " << sourceBoundsSize.z << ")\n"
        << "  Debug Up = " << (sourceIsZUp ? "Z -> Y" : "Y") << '\n'
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