// Raven/Animation/HumanoidAnimationProfileSerialization.h
#pragma once

#include "Raven/Animation/HumanoidAnimationProfile.h"

#include <string>

namespace Raven
{

// JSON Profile AssetとC++構造体の境界です。
// RuntimeやDemo LayerへFile I/Oを持ち込まず、文字列変換とFile I/Oをこのモジュールへ集約します。
bool SerializeHumanoidAnimationProfile(
    const HumanoidAnimationProfile& profile,
    std::string& outText,
    std::string* errorMessage = nullptr);

bool DeserializeHumanoidAnimationProfile(
    const std::string& text,
    HumanoidAnimationProfile& outProfile,
    std::string* errorMessage = nullptr);

bool SaveHumanoidAnimationProfile(
    const std::string& filePath,
    const HumanoidAnimationProfile& profile,
    std::string* errorMessage = nullptr);

bool LoadHumanoidAnimationProfile(
    const std::string& filePath,
    HumanoidAnimationProfile& outProfile,
    std::string* errorMessage = nullptr);

} // namespace Raven
