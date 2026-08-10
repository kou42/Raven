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
};

} // namespace Raven
