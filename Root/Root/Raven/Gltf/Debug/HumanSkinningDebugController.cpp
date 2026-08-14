// Raven/Gltf/Debug/HumanSkinningDebugController.cpp
#include "Raven/Gltf/Debug/HumanSkinningDebugController.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>
#include <vector>

#include "Raven/Core/Input.h"
#include "Raven/Core/KeyCodes.h"
#include "Raven/Math/MathQuatanion.h"

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

std::string NormalizeBoneName(const std::string& source)
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

std::string FindBoneName(
    const std::vector<std::string>& boneNames,
    const std::vector<std::string>& candidates)
{
    // まず完全一致を優先します。
    // "LeftArm" と "LeftForeArm" のような部分一致事故を避けるためです。
    for (const std::string& candidate : candidates)
    {
        const std::string normalizedCandidate = NormalizeBoneName(candidate);
        for (const std::string& boneName : boneNames)
        {
            if (NormalizeBoneName(boneName) == normalizedCandidate)
            {
                return boneName;
            }
        }
    }

    // Mixamoの "mixamorig:LeftArm" のようにnamespace prefixが付くケースでは、
    // 正規化後の末尾一致だけを許可します。中間部分一致にはしません。
    for (const std::string& candidate : candidates)
    {
        const std::string normalizedCandidate = NormalizeBoneName(candidate);
        for (const std::string& boneName : boneNames)
        {
            const std::string normalizedBone = NormalizeBoneName(boneName);
            if (EndsWith(normalizedBone, normalizedCandidate))
            {
                return boneName;
            }
        }
    }

    return {};
}

float ClampAngle(float angle, float maxAbsAngle)
{
    return std::clamp(angle, -maxAbsAngle, maxAbsAngle);
}

} // namespace

bool HumanSkinningDebugController::Initialize(
    SkinnedMeshSceneInstance& sceneInstance,
    std::size_t skinIndex,
    std::string* errorMessage)
{
    if (errorMessage != nullptr)
    {
        errorMessage->clear();
    }

    if (sceneInstance.IsValid() == false)
    {
        return SetError(errorMessage, "Human Skinning Debug対象SceneInstanceが無効です");
    }

    std::vector<std::string> boneNames;
    if (sceneInstance.GetBoneNames(skinIndex, boneNames, errorMessage) == false)
    {
        return false;
    }

    if (boneNames.empty())
    {
        return SetError(errorMessage, "Human Skinning Debug対象SkeletonにBoneがありません");
    }

    m_SceneInstance = &sceneInstance;
    m_SkinIndex = skinIndex;
    m_BoneNames = std::move(boneNames);

    // よく使われる命名規則だけを候補にします。
    // 解決できなかった場合は空文字のままにし、誤ったBoneを動かすより安全側へ倒します。
    m_LeftUpperArmBoneName = FindBoneName(
        m_BoneNames,
        { "LeftUpperArm", "LeftArm", "upper_arm.L", "UpperArm_L" });

    m_LeftForeArmBoneName = FindBoneName(
        m_BoneNames,
        { "LeftForeArm", "LeftLowerArm", "forearm.L", "lower_arm.L", "ForeArm_L" });

    m_HeadBoneName = FindBoneName(
        m_BoneNames,
        { "Head" });

    m_UpperArmAngle = 0.0f;
    m_ForeArmAngle = 0.0f;
    m_HeadAngle = 0.0f;
    m_WasResetPressed = false;

    return true;
}

void HumanSkinningDebugController::Reset()
{
    m_SceneInstance = nullptr;
    m_SkinIndex = 0u;
    m_BoneNames.clear();
    m_LeftUpperArmBoneName.clear();
    m_LeftForeArmBoneName.clear();
    m_HeadBoneName.clear();
    m_UpperArmAngle = 0.0f;
    m_ForeArmAngle = 0.0f;
    m_HeadAngle = 0.0f;
    m_WasResetPressed = false;
}

bool HumanSkinningDebugController::ApplyPose(std::string* errorMessage)
{
    if (m_SceneInstance == nullptr)
    {
        return SetError(errorMessage, "Human Skinning Debug Controllerが初期化されていません");
    }

    // 各BoneはBind Pose基準の絶対offsetとして設定します。
    // 前Frameの現在Rotationへ掛け続けないため、操作時間に比例した誤差累積が発生しません。
    if (m_LeftUpperArmBoneName.empty() == false)
    {
        const math::Quat rotation = math::Quat::FromAxisAngle(m_UpperArmAxis, m_UpperArmAngle);
        if (m_SceneInstance->SetBoneLocalRotationOffsetFromBind(
                m_SkinIndex,
                m_LeftUpperArmBoneName,
                rotation,
                errorMessage) == false)
        {
            return false;
        }
    }

    if (m_LeftForeArmBoneName.empty() == false)
    {
        const math::Quat rotation = math::Quat::FromAxisAngle(m_ForeArmAxis, m_ForeArmAngle);
        if (m_SceneInstance->SetBoneLocalRotationOffsetFromBind(
                m_SkinIndex,
                m_LeftForeArmBoneName,
                rotation,
                errorMessage) == false)
        {
            return false;
        }
    }

    if (m_HeadBoneName.empty() == false)
    {
        const math::Quat rotation = math::Quat::FromAxisAngle(m_HeadAxis, m_HeadAngle);
        if (m_SceneInstance->SetBoneLocalRotationOffsetFromBind(
                m_SkinIndex,
                m_HeadBoneName,
                rotation,
                errorMessage) == false)
        {
            return false;
        }
    }

    return true;
}

bool HumanSkinningDebugController::Update(float deltaTime, std::string* errorMessage)
{
    if (errorMessage != nullptr)
    {
        errorMessage->clear();
    }

    if (m_SceneInstance == nullptr)
    {
        return true;
    }

    if (std::isfinite(deltaTime) == false || deltaTime < 0.0f)
    {
        return SetError(errorMessage, "Human Skinning DebugのdeltaTimeが不正です");
    }

    bool poseChanged = false;
    const float deltaAngle = m_RotationSpeed * deltaTime;

    if (Input::IsKeyPressed(Key::U))
    {
        m_UpperArmAngle = ClampAngle(m_UpperArmAngle - deltaAngle, m_MaxUpperArmAngle);
        poseChanged = true;
    }
    if (Input::IsKeyPressed(Key::I))
    {
        m_UpperArmAngle = ClampAngle(m_UpperArmAngle + deltaAngle, m_MaxUpperArmAngle);
        poseChanged = true;
    }

    if (Input::IsKeyPressed(Key::J))
    {
        m_ForeArmAngle = ClampAngle(m_ForeArmAngle - deltaAngle, m_MaxForeArmAngle);
        poseChanged = true;
    }
    if (Input::IsKeyPressed(Key::K))
    {
        m_ForeArmAngle = ClampAngle(m_ForeArmAngle + deltaAngle, m_MaxForeArmAngle);
        poseChanged = true;
    }

    if (Input::IsKeyPressed(Key::M))
    {
        m_HeadAngle = ClampAngle(m_HeadAngle - deltaAngle, m_MaxHeadAngle);
        poseChanged = true;
    }
    if (Input::IsKeyPressed(Key::L))
    {
        m_HeadAngle = ClampAngle(m_HeadAngle + deltaAngle, m_MaxHeadAngle);
        poseChanged = true;
    }

    const bool resetPressed = Input::IsKeyPressed(Key::R);
    if (resetPressed && m_WasResetPressed == false)
    {
        m_UpperArmAngle = 0.0f;
        m_ForeArmAngle = 0.0f;
        m_HeadAngle = 0.0f;

        if (m_SceneInstance->ResetSkinToBindPose(m_SkinIndex, errorMessage) == false)
        {
            return false;
        }

        poseChanged = false;
    }
    m_WasResetPressed = resetPressed;

    if (poseChanged)
    {
        return ApplyPose(errorMessage);
    }

    return true;
}

} // namespace Gltf
} // namespace Raven
