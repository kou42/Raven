// Raven/Animation/SkinWeight.h
#pragma once

#include <array>
#include <cstddef>

#include "Raven/Animation/Bone.h"
#include "Raven/Math/Math.h"

namespace Raven
{

inline constexpr std::size_t MaxBoneInfluences = 4;

// ============================================================================
// SkinWeight
// ============================================================================
// 1頂点に影響するBone IndexとWeightを保持します。
// 最初のCPU Linear Blend Skinningでは4 Influence固定とし、GPU Skinningへ移行しても
// 同じ上限をそのままVertex Attributeへ写せる構成にします。
struct SkinWeight
{
    std::array<BoneIndex, MaxBoneInfluences> BoneIndices{
        InvalidBoneIndex, InvalidBoneIndex, InvalidBoneIndex, InvalidBoneIndex
    };

    std::array<float, MaxBoneInfluences> Weights{
        0.0f, 0.0f, 0.0f, 0.0f
    };

    float GetTotalWeight() const
    {
        float total = 0.0f;
        for (float weight : Weights)
            total += weight;
        return total;
    }

    // Weight合計を1へ正規化します。
    // 全Weightが0の場合は変形元を決められないためfalseを返します。
    bool Normalize(float eps = math::Epsilon)
    {
        const float total = GetTotalWeight();
        if (total <= eps)
            return false;

        const float inverseTotal = 1.0f / total;
        for (float& weight : Weights)
            weight *= inverseTotal;
        return true;
    }

    // 空いているInfluence slotへBoneを追加します。
    // 同一Boneが既に存在する場合はWeightを加算し、4枠を超える場合はfalseを返します。
    bool AddInfluence(BoneIndex boneIndex, float weight)
    {
        if (boneIndex == InvalidBoneIndex || weight <= 0.0f)
            return false;

        for (std::size_t i = 0; i < MaxBoneInfluences; ++i)
        {
            if (BoneIndices[i] == boneIndex)
            {
                Weights[i] += weight;
                return true;
            }
        }

        for (std::size_t i = 0; i < MaxBoneInfluences; ++i)
        {
            if (BoneIndices[i] == InvalidBoneIndex || Weights[i] <= 0.0f)
            {
                BoneIndices[i] = boneIndex;
                Weights[i] = weight;
                return true;
            }
        }

        return false;
    }
};

} // namespace Raven
