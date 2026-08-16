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
        - column1.x * (column0.y * column2.z - column0.z * column2.x)
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

    // ========================================================================
    // Humanoid直立補正後のWorld MatrixをSceneのTRSへ戻す
    // ========================================================================
    // Skeleton Bind Poseから求めた直立補正は、各Primitiveの既存World Transformの左側へ
    // 同じ行列として適用します。TransformComponentは行列を直接保持せずTRSを保持するため、
    // 補正後World MatrixをPosition / Rotation / Scaleへ分解して書き戻す必要があります。
    //
    // ここでShearやReflectionを「それらしいTRS」へ近似すると、Body / Clothes間の相対配置や
    // Skinning結果が静かに壊れて原因追跡が難しくなります。そのため、このDebug経路で安全に
    // 表現できないMatrixは明示的に拒否します。
    if (std::fabs(matrix[3][0]) > AffineTolerance
        || std::fabs(matrix[3][1]) > AffineTolerance
        || std::fabs(matrix[3][2]) > AffineTolerance
        || std::fabs(matrix[3][3] - 1.0f) > AffineTolerance)
    {
        return SetError(errorMessage, "Humanoid直立補正後TransformがAffine TRSではありません");
    }

    // T * R * S の3x3部分では各列が「回転後のBasis Axis * Scale」になります。
    // そのため各列長からScaleを取り出し、正規化列から純粋なRotation Basisを復元します。
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

    // T * R * Sで表現できる行列なら、Scaleを除いた3本のBasis Axisは互いに直交します。
    // 非直交ならShearが含まれているため、TransformComponentへ情報を落とさず失敗させます。
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
        // Reflection / 負Scaleは、どの軸へ符号を戻すかの規約を決めないとEuler値が不安定です。
        // Debug表示の都合で推測せず、対応範囲外として明示します。
        return SetError(errorMessage, "Humanoid直立補正後TransformのReflection/負Scaleは未対応です");
    }

    // TransformComponent::GetTransform()の Rx * Ry * Rz 規約へ戻します。
    // Spawner側のWorld Transform分解と同じ数学規約を使い、直立補正後もScene側のTransform表現を
    // 一貫させます。
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
        // Gimbal LockではX/Zを一意に分離できません。
        // Z=0を代表解とし、同じRotation Matrixを再現できるXを選びます。
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
    // Humanoid Bone名はExporterごとに "mixamorig:Hips" や "Armature_Hips" などの差があります。
    // 大文字小文字と区切り記号の差だけでSemantic Bone解決に失敗しないよう、比較用文字列では
    // 英数字だけを残して小文字化します。元のNode名そのものは変更しません。
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
    // まず完全一致を優先します。
    // 完全一致が無い場合のみsuffix一致へ進むことで、例えば "HeadTop" を "Head" として
    // 早期に誤認することを避けつつ、"mixamorig:Head" のようなnamespace付きNodeを解決します。
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
    // ========================================================================
    // 1. glTF Node階層を正規経路で読み、Bind PoseのGlobal Transformを構築
    // ========================================================================
    // Mesh形状から方向を推測せず、glTF Node/Skeletonが持つ意味情報を直立方向の正規データにします。
    // NodeHierarchyを使うことで、Root / Armature / Joint間に存在する親Transformも含めた位置を得ます。
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
    // 2. Humanoidの「上方向」はSkeleton Bind Poseの意味から決める
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

    // Joint Global PositionはglTF Scene Spaceで得られるため、Scene配置と同じ
    // glTF -> Raven World変換を通してから方向ベクトルを作ります。
    // 現在この基底変換はIdentityですが、境界を明示しておくことで将来の座標系変更でも
    // Humanoid判定だけが別規約になることを防ぎます。
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

    // ========================================================================
    // 3. Hips -> HeadをRaven +Yへ向ける最短回転を作る
    // ========================================================================
    // sourceUpと+Yが同方向なら補正不要です。
    if (clampedDot >= 1.0f - 1.0e-5f)
    {
        outRotation = math::Mat4::Identity();
    }
    else if (clampedDot <= -1.0f + 1.0e-5f)
    {
        // 完全な反対向きではCross(sourceUp, targetUp)が0になり回転軸を作れません。
        // +Xを安定した180度回転軸として選び、人物の上下だけを確実に反転させます。
        outRotation = math::Quat::FromAxisAngle(
            math::Vec3{ 1.0f, 0.0f, 0.0f },
            3.14159265358979323846f).ToMat4();
    }
    else
    {
        // 一般ケースではCrossが両方向に垂直な最短回転軸、acos(dot)が回転角になります。
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
    // ========================================================================
    // Human全体へ共通のWorld Space直立回転を適用
    // ========================================================================
    // 各PrimitiveのLocal Transformを個別に補正すると、Body / Clothesなどの相対配置を壊します。
    // そのため、Spawn時に構築済みのWorld Transformの左側へ全Primitive共通の回転を掛けます。
    //
    //   M_correctedWorld = R_upright * M_importedWorld
    //
    // Skeleton / inverseBindMatrices / Mesh Local Spaceは変更せず、Scene表示上のHuman全体だけを
    // 回転させるため、SkinningのMesh Local / Bind Space契約を維持できます。
    //
    // 重要:
    // ここではTransformComponentを書き換えるため、primitiveを非const参照で受けます。
    // Entity::GetComponent()にはconst/non-const overloadがあり、const Entityから取得した場合は
    // const T&になるため、mutableなScene Entityを編集するこの処理では非constが必要です。
    for (SpawnedSkinnedPrimitive& primitive : primitives)
    {
        if (static_cast<bool>(primitive.EntityHandle) == false
            || primitive.EntityHandle.HasComponent<TransformComponent>() == false)
        {
            continue;
        }

        TransformComponent& transform = primitive.EntityHandle.GetComponent<TransformComponent>();
        const math::Mat4 correctedWorld = uprightRotation * transform.GetTransform();

        // 単純にRotation.x等へ加算すると、既存Node Rotationとの合成順によって別の回転になります。
        // 行列として正しく左乗算してからTRSへ再分解することで、任意の既存Node Transformを保ちます。
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

    // ========================================================================
    // 1. Skeleton Bind PoseからHuman全体の直立回転を決定して適用
    // ========================================================================
    // 旧実装のようにAABBのX/Y/ZサイズからUp軸を推測しません。
    // AABBはT-Poseのarm span、服や装備、Animation Poseによって形状が変わるため、方向情報の
    // 正規データにはできません。Hips/Pelvis -> HeadというSkeletonの意味情報だけを使います。
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
    // 2. 直立補正後のWorld AABBを計算
    // ========================================================================
    // ここからのAABBは「方向判定」ではなく、Debug表示のサイズ・Center/Floor合わせだけに使います。
    // Hips/Pelvis -> Head方向は既にRaven +Yへ揃っているため、高さは明示的にY幅を使えます。
    //
    // Primitiveごとの頂点はMesh Local Spaceにあります。複数Primitiveが別Node Transformを持つ
    // 可能性があるためLocal座標を直接統合せず、各Entityの現在World Transformを適用してから
    // 全Primitive分を同じWorld Space AABBへ統合します。
    // この走査ではEntityを書き換えないため、primitiveはconst参照で扱います。
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

    // Skeleton基準で直立済みなので、人物の表示高さはRaven WorldのY幅です。
    // ここでX/Zとの大小比較は行わず、形状からUp軸を再推測しないことが重要です。
    const float sourceHeight = sourceBoundsSize.y;
    if (sourceHeight <= MinimumHeight)
    {
        return SetError(errorMessage, "Skeleton直立補正後のHuman高さが0に近すぎます");
    }

    // ========================================================================
    // 3. 高さをTargetHeightへUniform Scaleし、Center / Floorを正規化
    // ========================================================================
    // Uniform Scaleだけを使うことでHumanの縦横比やPrimitive間の相対Scaleを変えません。
    // X/ZはBounds Centerを原点へ、YはBounds MinをFloor(Y=0)へ合わせます。
    // Centerを単純にY=TargetHeight/2へ置く方式より、足先と頭頂が非対称でも確実に接地できます。
    const float uniformScale = TargetHeight / sourceHeight;
    const math::Vec3 debugTranslation{
        -sourceBoundsCenter.x * uniformScale,
        -sourceBoundsMin.y * uniformScale,
        -sourceBoundsCenter.z * uniformScale
    };

    // ========================================================================
    // 4. Human全体へ同じWorld Space Scale / Translationを適用
    // ========================================================================
    // 各Primitiveへ共通のDebug表示変換を適用することで、Body / Clothes等の相対配置を維持します。
    // 直立回転は既にApplyHumanoidUprightRotation()で完了しているため、ここではRotationに触れません。
    //
    //   M_debugWorld = T_debug * S_uniform * M_uprightWorld
    //
    // Skinning自体はMesh Local Spaceで完結しており、Skeleton / inverseBindMatrices / SkinWeightには
    // 一切補正を入れません。ここはあくまでScene EntityのDebug表示位置・サイズだけを担当します。
    for (SpawnedSkinnedPrimitive& primitive : primitives)
    {
        if (static_cast<bool>(primitive.EntityHandle) == false
            || primitive.EntityHandle.HasComponent<TransformComponent>() == false)
        {
            continue;
        }

        TransformComponent& transform = primitive.EntityHandle.GetComponent<TransformComponent>();

        // 左からT_debug * S_uniformを掛けるのと同じWorld Position更新です。
        // Scaleも全軸同倍率なので既存Rotationとの組み合わせで軸歪みを作りません。
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

    // 相対パスの場合、カレントワーキングディレクトリから解決されます。
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
    // GetPrimitives()にはconst/non-const overloadを用意しているため、const_castを使わず
    // SceneInstance側の所有権境界を保ったまま明示的にmutableなPrimitive配列を取得します。
    std::vector<SpawnedSkinnedPrimitive>& primitives = m_HumanInstance.GetPrimitives();
    if (primitives.empty())
    {
        std::cerr << "[HumanSkinning] Spawn後のPrimitiveが0件です。\n";
        DestroyHuman();
        return false;
    }

    // ========================================================================
    // Human Debug固有の直立補正と表示正規化
    // ========================================================================
    // glTF +Y upというScene座標系規約と、Humanが実際にどちらを向いてBindされているかは別問題です。
    // ここではNodeHierarchyからJoint Bind Poseを読み、Hips/Pelvis -> HeadをHumanの+Upとして
    // Raven +Yへ合わせます。Geometry AABBによるUp軸推測は行いません。
    //
    // 直立後は全PrimitiveのWorld AABBから表示高さ・Center・Floorだけを求めます。
    // Human.glb固有の単位やScene Node位置に依存せず、既定Cameraから手動Skinning結果を確認できる
    // サイズ・位置へ揃えることが、このDebug Layer側の正規化の責務です。
    //
    // Skeleton / inverseBindMatrices / Mesh Local頂点そのものは変更せず、Spawn済みEntity全体へ
    // 共通World Transformを与えるためSkinningの空間契約は維持されます。
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
