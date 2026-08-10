// Raven/Animation/SkinnedMeshData.h
#pragma once

#include <cstddef>
#include <utility>
#include <vector>

#include "Raven/Animation/SkinWeight.h"
#include "Raven/Animation/Skeleton.h"
#include "Raven/Math/MathVector.h"

namespace Raven
{

// ============================================================================
// SkinnedMeshData
// ============================================================================
// Skeletal Deformation専用の「変形前データ」を保持します。
// MeshVertex自体へBoneIndex/Weightをまだ混ぜず、CPU Skinning段階ではRenderer入力と
// Skinning入力を分離します。GPU Skinningへ移行する段階でVertex Attribute化できます。
class SkinnedMeshData
{
public:
    SkinnedMeshData() = default;

    SkinnedMeshData(
        std::vector<math::Vec3> bindPositions,
        std::vector<SkinWeight> skinWeights)
        : m_BindPositions(std::move(bindPositions)),
          m_SkinWeights(std::move(skinWeights))
    {
    }

    const std::vector<math::Vec3>& GetBindPositions() const
    {
        return m_BindPositions;
    }

    const std::vector<SkinWeight>& GetSkinWeights() const
    {
        return m_SkinWeights;
    }

    std::size_t GetVertexCount() const
    {
        return m_BindPositions.size();
    }

    bool IsConsistent() const
    {
        return m_BindPositions.size() == m_SkinWeights.size();
    }

    // Skeletonと組み合わせて使用可能かを検証します。
    // 各頂点は最低1つの有効Influenceを持ち、参照BoneはSkeleton内に存在し、
    // Weight合計はほぼ1であることを要求します。
    bool Validate(const Skeleton& skeleton, float weightTolerance = 1.0e-4f) const
    {
        if (!IsConsistent())
            return false;

        for (const SkinWeight& skinWeight : m_SkinWeights)
        {
            bool hasInfluence = false;
            float totalWeight = 0.0f;

            for (std::size_t i = 0; i < MaxBoneInfluences; ++i)
            {
                const float weight = skinWeight.Weights[i];
                if (weight <= 0.0f)
                    continue;

                const BoneIndex boneIndex = skinWeight.BoneIndices[i];
                if (!skeleton.IsValidBoneIndex(boneIndex))
                    return false;

                hasInfluence = true;
                totalWeight += weight;
            }

            if (!hasInfluence)
                return false;

            const float difference = totalWeight - 1.0f;
            if (difference < -weightTolerance || difference > weightTolerance)
                return false;
        }

        return true;
    }

private:
    std::vector<math::Vec3> m_BindPositions;
    std::vector<SkinWeight> m_SkinWeights;
};

} // namespace Raven
