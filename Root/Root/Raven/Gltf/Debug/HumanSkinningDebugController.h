// Raven/Gltf/Debug/HumanSkinningDebugController.h
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "Raven/Gltf/SkinnedMeshSceneSpawner.h"
#include "Raven/Math/MathVector.h"

namespace Raven
{
namespace Gltf
{

// ============================================================================
// HumanSkinningDebugController
// ============================================================================
// glTF Humanの「Import -> Skeleton -> SkinWeight -> SkeletalMeshDeformer」経路を
// AnimationClip実装前に目視確認するための一時的な手動Pose Controllerです。
//
// Keyboard:
//   U / I : Left UpperArm - / +
//   J / K : Left ForeArm  - / +
//   M / L : Head          - / +
//   R     : Bind PoseへReset
//
// 重要:
// - 回転は常にBind Pose基準のoffsetとして適用します。
// - 前FrameのPoseへdeltaを累積しないため、長時間操作しても数値誤差が蓄積しません。
// - Bone名はAsset依存なので、Mixamo/一般/Blender系候補から解決し、見つからないBoneを
//   勝手に別Boneへ割り当てません。
class HumanSkinningDebugController
{
public:
    bool Initialize(
        SkinnedMeshSceneInstance& sceneInstance,
        std::size_t skinIndex,
        std::string* errorMessage = nullptr);

    bool Update(float deltaTime, std::string* errorMessage = nullptr);

    void Reset();

    bool IsInitialized() const
    {
        return m_SceneInstance != nullptr;
    }

    const std::vector<std::string>& GetBoneNames() const
    {
        return m_BoneNames;
    }

    const std::string& GetLeftUpperArmBoneName() const
    {
        return m_LeftUpperArmBoneName;
    }

    const std::string& GetLeftForeArmBoneName() const
    {
        return m_LeftForeArmBoneName;
    }

    const std::string& GetHeadBoneName() const
    {
        return m_HeadBoneName;
    }

private:
    bool ApplyPose(std::string* errorMessage);

private:
    SkinnedMeshSceneInstance* m_SceneInstance = nullptr;
    std::size_t m_SkinIndex = 0u;

    std::vector<std::string> m_BoneNames;
    std::string m_LeftUpperArmBoneName;
    std::string m_LeftForeArmBoneName;
    std::string m_HeadBoneName;

    // Bone Local Space上の回転軸です。
    // RigによってBone axis conventionは異なるため、Animation Import前の目視確認用として
    // UpperArm/ForeArmはZ、HeadはYを既定値にしています。
    math::Vec3 m_UpperArmAxis{ 0.0f, 0.0f, 1.0f };
    math::Vec3 m_ForeArmAxis{ 0.0f, 0.0f, 1.0f };
    math::Vec3 m_HeadAxis{ 0.0f, 1.0f, 0.0f };

    float m_UpperArmAngle = 0.0f;
    float m_ForeArmAngle = 0.0f;
    float m_HeadAngle = 0.0f;

    float m_RotationSpeed = 1.25f;
    float m_MaxUpperArmAngle = 1.4f;
    float m_MaxForeArmAngle = 2.2f;
    float m_MaxHeadAngle = 1.0f;

    bool m_WasResetPressed = false;
};

} // namespace Gltf
} // namespace Raven
