// Raven/Gltf/Debug/HumanSkinningDebugLayer.cpp
#include "Raven/Gltf/Debug/HumanSkinningDebugLayer.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#include "Raven/Gltf/GltfCoordinateSystem.h"
#include "Raven/Gltf/NodeHierarchy.h"
#include "Raven/Math/MathQuatanion.h"
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

bool SetError(std::string* errorMessage, const std::string& message)
{
    if (errorMessage != nullptr)
    {
        *errorMessage = message;
    }

    return false;
}

math::Vec3 TransformPosition(const math::Mat4& matrix, const math::Vec3& position)
{
    const math::Vec4 transformed = matrix * math::Vec4{ position.x, position.y, position.z, 1.0f };
    return { transformed.x, transformed.y, transformed.z };
}

float Dot(const math::Vec3& a, const math::Vec3& b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

float Length(const math::Vec3& value)
{
    return std::sqrt(Dot(value, value));
}

math::Vec3 Divide(const math::Vec3& value, float scalar)
{
    return math::Vec3{
        value.x / scalar,
        value.y / scalar,
        value.z / scalar
    };
}

float Determinant3x3(
    const math::Vec3& column0,
    const math::Vec3& column1,
    const math::Vec3& column2)
{
    return column0.x * (column1.y * column2.z - column1.z * column2.y)
        - column1.x * (column0.y * column2.z - column0.z * column2.y)
        + column2.x * (column0.y * column1.z - column0.z * column1.y);
}

bool DecomposeWorldTransform(
    const math::Mat4& matrix,
    TransformComponent& outTransform,
    std::string* errorMessage)
{
    constexpr float AffineTolerance = 1.0e-4f;
    constexpr float ScaleTolerance = 1.0e-6f;
    constexpr float OrthogonalTolerance = 2.0e-4f;

    // Skeleton由来の補正回転は全Primitive World Transformの左側へ掛けます。
    // TransformComponentはTRSしか保持できないため、補正後行列をScene表現へ戻す前に
    // Affine / Orthogonal条件を明示的に検証します。黙ってShearを近似しないことが重要です。
    if (std::fabs(matrix[3][0]) > AffineTolerance
        || std::fabs(matrix[3][1]) > AffineTolerance
        || std::fabs(matrix[3][2]) > AffineTolerance
        || std::fabs(matrix[3][3] - 1.0f) > AffineTolerance)
    {
        return SetError(errorMessage, "Humanoid直立補正後TransformがAffine TRSではありません");
    }

    const math::Vec3 column0{ matrix[0][0], matrix[1][0], matrix[2][0] };
    const math::Vec3 column1{ matrix[0][1], matrix[1][1], matrix[2][1] };
    const math::Vec3 column2{ matrix[0][2], matrix[1][2], matrix[2][2] };

    const float scaleX = Length(column0);
    const float scaleY = Length(column1);
    const float scaleZ = Length(column2);
    if (std::isfinite(scaleX) == false
        || std::isfinite(scaleY) == false
        || std::isfinite(scaleZ) == false
        || scaleX <= ScaleTolerance
        || scaleY <= ScaleTolerance
        || scaleZ <= ScaleTolerance)
    {
        return SetError(errorMessage, "Humanoid直立補正後TransformのScaleが特異です");
    }

    const math::Vec3 axisX = Divide(column0, scaleX);
    const math::Vec3 axisY = Divide(column1, scaleY);
    const math::Vec3 axisZ = Divide(column2, scaleZ);

    if (std::fabs(Dot(axisX, axisY)) > OrthogonalTolerance
        || std::fabs(Dot(axisX, axisZ)) > OrthogonalTolerance
        || std::fabs(Dot(axisY, axisZ)) > OrthogonalTolerance)
    {
        return SetError(errorMessage, "Humanoid直立補正後TransformにShearが含まれています");
    }

    const float determinant = Determinant3x3(axisX, axisY, axisZ);
    if (std::isfinite(determinant) == false
        || std::fabs(determinant - 1.0f) > 1.0e-3f)
    {
        return SetError(errorMessage, "Humanoid直立補正後TransformのReflection/負Scaleは未対応です");
    }

    // TransformComponent::GetTransform()の Rx * Ry * Rz 規約へ戻します。
    const float r00 = axisX.x;
    const float r10 = axisX.y;
    const float r01 = axisY.x;
    const float r11 = axisY.y;
    const float r02 = axisZ.x;
    const float r12 = axisZ.y;
    const float r22 = axisZ.z;

    const float clampedSinY = r02 < -1.0f ? -1.0f : (r02 > 1.0f ? 1.0f : r02);
    const float rotationY = std::asin(clampedSinY);
    const float cosY = std::cos(rotationY);

    float rotationX = 0.0f;
    float rotationZ = 0.0f;
    if (std::fabs(cosY) > 1.0e-5f)
    {
        rotationX = std::atan2(-r12, r22);
        rotationZ = std::atan2(-r01, r00);
    }
    else
    {
        const float signY = clampedSinY >= 0.0f ? 1.0f : -1.0f;
        rotationX = std::atan2(signY * r10, r11);
        rotationZ = 0.0f;
    }

    if (std::isfinite(rotationX) == false
        || std::isfinite(rotationY) == false
        || std::isfinite(rotationZ) == false)
    {
        return SetError(errorMessage, "Humanoid直立補正後TransformのEuler変換に失敗しました");
    }

    outTransform.Position = math::Vec3{ matrix[0][3], matrix[1][3], matrix[2][3] };
    outTransform.Rotation = math::Vec3{ rotationX, rotationY, rotationZ };
    outTransform.Scale = math::Vec3{ scaleX, scaleY, scaleZ };
    return true;
}

std::string NormalizeNodeName(const std::string& source)
{
    std::string normalized;
    normalized.reserve(source.size());

    for (const unsigned char character : source)
    {
        if (std::isalnum(character) == 0)
        {
            continue;
        }

        normalized.push_back(static_cast<char>(std::tolower(character)));
    }

    return normalized;
}

bool EndsWith(const std::string& value, const std::string& suffix)
{
    if (suffix.size() > value.size())
    {
        return false;
    }

    return value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::size_t FindNodeBySemanticName(
    const std::vector<Node>& nodes,
    const std::vector<std::string>& candidates)
{
    // まず完全一致を優先します。Mixamo等のnamespace付きNodeは次のsuffix一致で解決します。
    for (const std::string& candidate : candidates)
    {
        const std::string normalizedCandidate = NormalizeNodeName(candidate);
        for (std::size_t nodeIndex = 0u; nodeIndex < nodes.size(); ++nodeIndex)
        {
            if (NormalizeNodeName(nodes[nodeIndex].Name) == normalizedCandidate)
            {
                return nodeIndex;
            }
        }
    }

    for (const std::string& candidate : candidates)
    {
        const std::string normalizedCandidate = NormalizeNodeName(candidate);
        for (std::size_t nodeIndex = 0u; nodeIndex < nodes.size(); ++nodeIndex)
        {
            if (EndsWith(NormalizeNodeName(nodes[nodeIndex].Name), normalizedCandidate))
            {
                return nodeIndex;
            }
        }
    }

    return InvalidGltfIndex;
}

bool BuildHumanoidUprightRotation(
    const std::string& modelPath,
    math::Mat4& outRotation,
    math::Vec3& outSourceUp,
    std::string& outLowerBoneName,
    std::string& outUpperBoneName,
    std::string* errorMessage)
{
    NodeHierarchy hierarchy;
    if (NodeHierarchy::LoadFromGlb(modelPath, hierarchy, errorMessage) == false)
    {
        return false;
    }

    std::vector<math::Mat4> globalTransforms;
    if (hierarchy.BuildGlobalTransforms(globalTransforms, errorMessage) == false)
    {
        return false;
    }

    const std::vector<Node>& nodes = hierarchy.GetNodes();
    if (nodes.size() != globalTransforms.size())
    {
        return SetError(errorMessage, "Humanoid方向判定用Node数とGlobal Transform数が一致しません");
    }

    // ========================================================================
    // Humanoidの「上方向」はSkeleton Bind Poseの意味から決める
    // ========================================================================
    // glTFの座標系自体は常に+Y upですが、Humanモデルがその+Yへ直立しているとは限りません。
    // Raven_human_test.glbでは人物のBind Poseが横向きに格納されているため、Scene基底規約と
    // Humanoid姿勢方向を分離して扱います。
    //
    // Geometry AABBの長軸は使わず、Hips/Pelvis -> Headという意味的なJoint方向を採用します。
    // これならT-Poseの腕幅やMesh形状、服の大きさに結果が左右されません。
    const std::size_t lowerNodeIndex = FindNodeBySemanticName(
        nodes,
        { "Hips", "Pelvis" });
    const std::size_t upperNodeIndex = FindNodeBySemanticName(
        nodes,
        { "Head" });

    if (lowerNodeIndex == InvalidGltfIndex)
    {
        return SetError(errorMessage, "Humanoid直立判定に必要なHips/Pelvis Jointを解決できませんでした");
    }
    if (upperNodeIndex == InvalidGltfIndex)
    {
        return SetError(errorMessage, "Humanoid直立判定に必要なHead Jointを解決できませんでした");
    }

    const math::Mat4 gltfToRavenWorld = BuildGltfToRavenWorldTransform();
    const math::Vec3 lowerPosition = TransformPosition(
        gltfToRavenWorld * globalTransforms[lowerNodeIndex],
        math::Vec3{ 0.0f, 0.0f, 0.0f });
    const math::Vec3 upperPosition = TransformPosition(
        gltfToRavenWorld * globalTransforms[upperNodeIndex],
        math::Vec3{ 0.0f, 0.0f, 0.0f });

    const math::Vec3 sourceUpVector = upperPosition - lowerPosition;
    const float sourceUpLength = sourceUpVector.Length();
    if (std::isfinite(sourceUpLength) == false || sourceUpLength <= 1.0e-5f)
    {
        return SetError(errorMessage, "Hips/Pelvis -> Head方向が0に近くHumanoid直立方向を決定できません");
    }

    const math::Vec3 sourceUp = sourceUpVector / sourceUpLength;
    const math::Vec3 targetUp{ 0.0f, 1.0f, 0.0f };
    const float rawDot = math::Vec3::Dot(sourceUp, targetUp);
    const float clampedDot = rawDot < -1.0f ? -1.0f : (rawDot > 1.0f ? 1.0f : rawDot);

    // sourceUpと+Yが同方向なら補正不要です。
    if (clampedDot >= 1.0f - 1.0e-5f)
    {
        outRotation = math::Mat4::Identity();
    }
    else if (clampedDot <= -1.0f + 1.0e-5f)
    {
        // 完全な反対向きではCross軸が0になるため、+Xを安定した180度回転軸として使います。
        outRotation = math::Quat::FromAxisAngle(
            math::Vec3{ 1.0f, 0.0f, 0.0f },
            3.14159265358979323846f).ToMat4();
    }
    else
    {
        const math::Vec3 rotationAxis = math::Vec3::Cross(sourceUp, targetUp).Normalized();
        const float rotationAngle = std::acos(clampedDot);
        outRotation = math::Quat::FromAxisAngle(rotationAxis, rotationAngle).ToMat4();
    }

    outSourceUp = sourceUp;
    outLowerBoneName = nodes[lowerNodeIndex].Name;
    outUpperBoneName = nodes[upperNodeIndex].Name;
    return true;
}

bool ApplyHumanoidUprightRotation(
    std::vector<SpawnedSkinnedPrimitive>& primitives,
    const math::Mat4& uprightRotation,
    std::string* errorMessage)
{
    // 全Primitiveへ同じWorld Space回転を左から掛けます。
    // Skeleton/InverseBind/Mesh Local Spaceは変更せず、Scene表示上のHuman全体だけを回転させます。
    // Body/Clothes等が複数Primitiveに分割されていても相対配置は維持されます。
    for (SpawnedSkinnedPrimitive& primitive : primitives)
    {
        if (static_cast<bool>(primitive.EntityHandle) == false
            || primitive.EntityHandle.HasComponent<TransformComponent>() == false)
        {
            continue;
        }

        TransformComponent& transform = primitive.EntityHandle.GetComponent<TransformComponent>();
        const math::Mat4 correctedWorld = uprightRotation * transform.GetTransform();

        TransformComponent correctedTransform{};
        if (DecomposeWorldTransform(correctedWorld, correctedTransform, errorMessage) == false)
        {
            return false;
        }

        transform = correctedTransform;
    }

    return true;
}

bool NormalizeHumanForDebugView(
    const std::string& modelPath,
    std::vector<SpawnedSkinnedPrimitive>& primitives,
    std::string* errorMessage)
{
    constexpr float TargetHeight = 20.0f;
    constexpr float MinimumHeight = 1.0e-5f;

    math::Mat4 uprightRotation = math::Mat4::Identity();
    math::Vec3 sourceUp{};
    std::string lowerBoneName;
    std::string upperBoneName;
    if (BuildHumanoidUprightRotation(
            modelPath,
            uprightRotation,
            sourceUp,
            lowerBoneName,
            upperBoneName,
            errorMessage) == false)
    {
        return false;
    }

    if (ApplyHumanoidUprightRotation(primitives, uprightRotation, errorMessage) == false)
    {
        return false;
    }

    const float maxFloat = std::numeric_limits<float>::max();
    math::Vec3 sourceBoundsMin{ maxFloat, maxFloat, maxFloat };
    math::Vec3 sourceBoundsMax{ -maxFloat, -maxFloat, -maxFloat };
    bool hasVertex = false;

    // ========================================================================
    // 直立補正後のAABBは「サイズ・Center/Floor合わせ」にだけ使用
    // ========================================================================
    // Up軸決定にはAABBを一切使用しません。ここへ来た時点でSkeleton Bind Poseの
    // Hips/Pelvis -> Head方向がRaven +Yへ揃っています。
    for (const SpawnedSkinnedPrimitive& primitive : primitives)
    {
        if (static_cast<bool>(primitive.EntityHandle) == false
            || primitive.EntityHandle.HasComponent<TransformComponent>() == false
            || primitive.EntityHandle.HasComponent<MeshRendererComponent>() == false)
        {
            continue;
        }

        const TransformComponent& transform = primitive.EntityHandle.GetComponent<TransformComponent>();
        const MeshRendererComponent& meshRenderer = primitive.EntityHandle.GetComponent<MeshRendererComponent>();

        if (meshRenderer.Mesh == nullptr || meshRenderer.Mesh->GetGeometry() == nullptr)
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
        return SetError(errorMessage, "Human Debug表示用Boundsを計算できませんでした");
    }

    const math::Vec3 sourceBoundsCenter = (sourceBoundsMin + sourceBoundsMax) * 0.5f;
    const math::Vec3 sourceBoundsSize = sourceBoundsMax - sourceBoundsMin;
    const float sourceHeight = sourceBoundsSize.y;
    if (sourceHeight <= MinimumHeight)
    {
        return SetError(errorMessage, "Skeleton直立補正後のHuman高さが0に近すぎます");
    }

    const float uniformScale = TargetHeight / sourceHeight;
    const math::Vec3 debugTranslation{
        -sourceBoundsCenter.x * uniformScale,
        -sourceBoundsMin.y * uniformScale,
        -sourceBoundsCenter.z * uniformScale
    };

    for (SpawnedSkinnedPrimitive& primitive : primitives)
    {
        if (static_cast<bool>(primitive.EntityHandle) == false
            || primitive.EntityHandle.HasComponent<TransformComponent>() == false)
        {
            continue;
        }

        TransformComponent& transform = primitive.EntityHandle.GetComponent<TransformComponent>();
        transform.Position = debugTranslation + transform.Position * uniformScale;
        transform.Scale = transform.Scale * uniformScale;
    }

    std::cout
        << "[HumanSkinning] Debug Bounds after Skeleton upright alignment:\n"
        << "  Min    = (" << sourceBoundsMin.x << ", " << sourceBoundsMin.y << ", " << sourceBoundsMin.z << ")\n"
        << "  Max    = (" << sourceBoundsMax.x << ", " << sourceBoundsMax.y << ", " << sourceBoundsMax.z << ")\n"
        << "  Size   = (" << sourceBoundsSize.x << ", " << sourceBoundsSize.y << ", " << sourceBoundsSize.z << ")\n"
        << "  Coordinate System = " << GetGltfCoordinateSystemDescription() << '\n'
        << "  Humanoid Up Source = " << lowerBoneName << " -> " << upperBoneName << '\n'
        << "  Humanoid Up Vector = (" << sourceUp.x << ", " << sourceUp.y << ", " << sourceUp.z << ")\n"
        << "  Humanoid Up Target = (0, 1, 0)\n"
        << "  AABB used for orientation = false\n"
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
    std::vector<SpawnedSkinnedPrimitive>& primitives = m_HumanInstance.GetPrimitives();
    if (primitives.empty())
    {
        std::cerr << "[HumanSkinning] Spawn後のPrimitiveが0件です。\n";
        DestroyHuman();
        return false;
    }

    // ========================================================================
    // Human Debug固有の直立補正
    // ========================================================================
    // glTF +Y upというScene座標系規約と、Humanが実際にどちらを向いてBindされているかは別問題です。
    // ここではNodeHierarchyからJoint Bind Poseを読み、Hips/Pelvis -> HeadをHumanの+Upとして
    // Raven +Yへ合わせます。Geometry AABBによるUp軸推測は行いません。
    //
    // Skeleton / inverseBindMatrices / Mesh Local頂点そのものは変更せず、Spawn済みEntity全体へ
    // 共通World回転を与えるためSkinningの空間契約は維持されます。
    if (NormalizeHumanForDebugView(m_ModelPath, primitives, &errorMessage) == false)
    {
        std::cerr
            << "[HumanSkinning] Human.glbのSkeleton基準Debug正規化に失敗しました: "
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
