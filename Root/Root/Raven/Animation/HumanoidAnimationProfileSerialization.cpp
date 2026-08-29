// Raven/Animation/HumanoidAnimationProfileSerialization.cpp
#include "Raven/Animation/HumanoidAnimationProfileSerialization.h"

#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <utility>

namespace Raven
{
namespace
{
constexpr int CurrentProfileVersion = 1;

bool SetError(std::string* errorMessage, const std::string& message)
{
    if (errorMessage != nullptr)
    {
        *errorMessage = message;
    }
    return false;
}

bool ValidateProfile(const HumanoidAnimationProfile& profile, std::string* errorMessage)
{
    const HumanoidLocomotionProfile& locomotion = profile.Locomotion;
    if (locomotion.IdleAnimationName.empty()
        || locomotion.WalkAnimationName.empty()
        || locomotion.RunAnimationName.empty())
    {
        return SetError(errorMessage, "Animation名は空にできません");
    }
    if (std::isfinite(locomotion.IdleThreshold) == false
        || std::isfinite(locomotion.WalkThreshold) == false
        || std::isfinite(locomotion.RunThreshold) == false
        || locomotion.IdleThreshold < 0.0f
        || locomotion.WalkThreshold <= locomotion.IdleThreshold
        || locomotion.RunThreshold <= locomotion.WalkThreshold)
    {
        return SetError(errorMessage, "Thresholdは 0 <= Idle < Walk < Run を満たす必要があります");
    }
    if (std::isfinite(locomotion.WalkAuthoredMotionSpeed) == false
        || std::isfinite(locomotion.RunAuthoredMotionSpeed) == false
        || locomotion.WalkAuthoredMotionSpeed <= 0.0f
        || locomotion.RunAuthoredMotionSpeed <= locomotion.WalkAuthoredMotionSpeed)
    {
        return SetError(errorMessage, "Authored Motion Speedは 0 < Walk < Run を満たす必要があります");
    }
    return true;
}

bool ReadKey(std::istringstream& stream, const char* expectedKey, std::string* errorMessage)
{
    std::string key;
    if ((stream >> key).fail() == true || key != expectedKey)
    {
        return SetError(errorMessage, std::string("Profile項目がありません: ") + expectedKey);
    }
    return true;
}

bool ReadStringField(
    std::istringstream& stream,
    const char* key,
    std::string& outValue,
    std::string* errorMessage)
{
    if (ReadKey(stream, key, errorMessage) == false)
    {
        return false;
    }
    if ((stream >> std::quoted(outValue)).fail() == true)
    {
        return SetError(errorMessage, std::string("Profile文字列を読み取れません: ") + key);
    }
    return true;
}

bool ReadFloatField(
    std::istringstream& stream,
    const char* key,
    float& outValue,
    std::string* errorMessage)
{
    if (ReadKey(stream, key, errorMessage) == false)
    {
        return false;
    }
    if ((stream >> outValue).fail() == true)
    {
        return SetError(errorMessage, std::string("Profile数値を読み取れません: ") + key);
    }
    return true;
}
} // namespace

bool SerializeHumanoidAnimationProfile(
    const HumanoidAnimationProfile& profile,
    std::string& outText,
    std::string* errorMessage)
{
    if (errorMessage != nullptr)
    {
        errorMessage->clear();
    }
    if (ValidateProfile(profile, errorMessage) == false)
    {
        return false;
    }

    const HumanoidLocomotionProfile& locomotion = profile.Locomotion;
    std::ostringstream stream;
    stream << std::setprecision(9);
    stream << "RavenHumanoidAnimationProfile " << CurrentProfileVersion << '\n';
    stream << "IdleAnimationName " << std::quoted(locomotion.IdleAnimationName) << '\n';
    stream << "WalkAnimationName " << std::quoted(locomotion.WalkAnimationName) << '\n';
    stream << "RunAnimationName " << std::quoted(locomotion.RunAnimationName) << '\n';
    stream << "IdleThreshold " << locomotion.IdleThreshold << '\n';
    stream << "WalkThreshold " << locomotion.WalkThreshold << '\n';
    stream << "RunThreshold " << locomotion.RunThreshold << '\n';
    stream << "WalkAuthoredMotionSpeed " << locomotion.WalkAuthoredMotionSpeed << '\n';
    stream << "RunAuthoredMotionSpeed " << locomotion.RunAuthoredMotionSpeed << '\n';
    outText = stream.str();
    return true;
}

bool DeserializeHumanoidAnimationProfile(
    const std::string& text,
    HumanoidAnimationProfile& outProfile,
    std::string* errorMessage)
{
    if (errorMessage != nullptr)
    {
        errorMessage->clear();
    }

    std::istringstream stream(text);
    std::string signature;
    int version = 0;
    if ((stream >> signature >> version).fail() == true
        || signature != "RavenHumanoidAnimationProfile"
        || version != CurrentProfileVersion)
    {
        return SetError(errorMessage, "Humanoid Animation Profileの形式またはVersionが不正です");
    }

    HumanoidAnimationProfile profile{};
    HumanoidLocomotionProfile& locomotion = profile.Locomotion;
    if (ReadStringField(
            stream, "IdleAnimationName", locomotion.IdleAnimationName, errorMessage) == false
        || ReadStringField(
            stream, "WalkAnimationName", locomotion.WalkAnimationName, errorMessage) == false
        || ReadStringField(
            stream, "RunAnimationName", locomotion.RunAnimationName, errorMessage) == false
        || ReadFloatField(stream, "IdleThreshold", locomotion.IdleThreshold, errorMessage) == false
        || ReadFloatField(stream, "WalkThreshold", locomotion.WalkThreshold, errorMessage) == false
        || ReadFloatField(stream, "RunThreshold", locomotion.RunThreshold, errorMessage) == false
        || ReadFloatField(
            stream,
            "WalkAuthoredMotionSpeed",
            locomotion.WalkAuthoredMotionSpeed,
            errorMessage) == false
        || ReadFloatField(
            stream,
            "RunAuthoredMotionSpeed",
            locomotion.RunAuthoredMotionSpeed,
            errorMessage) == false)
    {
        return false;
    }
    if (ValidateProfile(profile, errorMessage) == false)
    {
        return false;
    }

    // 完全にParse・Validationできた場合だけ出力へ反映し、失敗時は呼び出し側のProfileを維持します。
    outProfile = std::move(profile);
    return true;
}

bool SaveHumanoidAnimationProfile(
    const std::string& filePath,
    const HumanoidAnimationProfile& profile,
    std::string* errorMessage)
{
    std::string text;
    if (SerializeHumanoidAnimationProfile(profile, text, errorMessage) == false)
    {
        return false;
    }

    std::ofstream file(filePath, std::ios::binary | std::ios::trunc);
    if (file.is_open() == false)
    {
        return SetError(errorMessage, "Humanoid Animation Profileを開けません: " + filePath);
    }
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (file.good() == false)
    {
        return SetError(errorMessage, "Humanoid Animation Profileを書き込めません: " + filePath);
    }
    return true;
}

bool LoadHumanoidAnimationProfile(
    const std::string& filePath,
    HumanoidAnimationProfile& outProfile,
    std::string* errorMessage)
{
    std::ifstream file(filePath, std::ios::binary);
    if (file.is_open() == false)
    {
        return SetError(errorMessage, "Humanoid Animation Profileを開けません: " + filePath);
    }
    std::ostringstream stream;
    stream << file.rdbuf();
    if (file.bad() == true)
    {
        return SetError(errorMessage, "Humanoid Animation Profileを読み取れません: " + filePath);
    }
    return DeserializeHumanoidAnimationProfile(stream.str(), outProfile, errorMessage);
}

} // namespace Raven
