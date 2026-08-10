// Raven/Animation/Bone.h
#pragma once

#include <cstdint>
#include <limits>
#include <string>

#include "Raven/Math/MathMatrix.h"
#include "Raven/Math/MathQuatanion.h"
#include "Raven/Math/MathVector.h"

namespace Raven
{

using BoneIndex = std::uint32_t;
inline constexpr BoneIndex InvalidBoneIndex = std::numeric_limits<BoneIndex>::max();

// ============================================================================
// BoneTransform
// ============================================================================
// Skeletal Animation内部で使用するTRS Transformです。
// Scene::TransformComponentはRenderer/Scene互換のためEuler角を保持していますが、
// Boneは補間・合成を頻繁に行うためQuaternionを正規の回転表現として保持します。
//
// 行列規約:
// Raven::math::Mat4は row-major / column-vector multiplication style です。
// そのためローカル変換は T * R * S の順で構築し、頂点には右側から作用します。
struct BoneTransform
{
    math::Vec3 Translation{ 0.0f, 0.0f, 0.0f };
    math::Quat Rotation = math::Quat::Identity();
    math::Vec3 Scale{ 1.0f, 1.0f, 1.0f };

    math::Mat4 ToMatrix() const
    {
        return math::Mat4::Translation(Translation)
            * Rotation.ToMat4()
            * math::Mat4::Scaling(Scale);
    }

    // ========================================================================
    // Inverse TRS
    // ========================================================================
    // M = T * R * S なので M^-1 = S^-1 * R^-1 * T^-1 です。
    // 一般4x4逆行列を毎Bone生成するのではなく、BoneがTRSであることを利用して
    // Bind Pose構築時に安価かつ意味の明確な逆変換を作ります。
    //
    // Scaleが0に近い場合は逆変換を定義できないため、Skeleton::AddBone()側で拒否します。
    math::Mat4 ToInverseMatrix() const
    {
        const math::Vec3 inverseScale{
            1.0f / Scale.x,
            1.0f / Scale.y,
            1.0f / Scale.z
        };

        const math::Quat inverseRotation = Rotation.Normalized().Conjugate();
        const math::Vec3 inverseTranslation{
            -Translation.x,
            -Translation.y,
            -Translation.z
        };

        return math::Mat4::Scaling(inverseScale)
            * inverseRotation.ToMat4()
            * math::Mat4::Translation(inverseTranslation);
    }
};

// ============================================================================
// Bone
// ============================================================================
// Skeletonを構成する「定義データ」です。
// Current Poseなどの実行時状態は持たせず、SkeletonPose側へ分離します。
// これにより1つのSkeletonを複数Entity / Animation Instanceから共有できます。
struct Bone
{
    std::string Name;
    BoneIndex Parent = InvalidBoneIndex;
    BoneTransform BindLocalTransform{};

    // Mesh Bind Space -> Bone Bind Space へ戻す行列です。
    // Skeleton::AddBone()時に階層を考慮して自動計算し、Runtime Skinningでは再計算しません。
    math::Mat4 InverseBindMatrix = math::Mat4::Identity();
};

} // namespace Raven
