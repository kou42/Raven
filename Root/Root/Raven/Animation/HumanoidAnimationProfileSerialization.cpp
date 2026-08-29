// Raven/Animation/HumanoidAnimationProfileSerialization.cpp
#include "Raven/Animation/HumanoidAnimationProfileSerialization.h"

#include "Raven/Core/JsonParser.h"
#include "Raven/Core/JsonWriter.h"

#include <cmath>
#include <fstream>
#include <sstream>
#include <utility>

namespace Raven
{
namespace
{
constexpr int CurrentProfileVersion = 1;
constexpr const char* ProfileType = "RavenHumanoidAnimationProfile";

bool SetError(std::string* errorMessage, const std::string& message)
{
    if (errorMessage != nullptr) { *errorMessage = message; }
    return false;
}

bool ValidateProfile(const HumanoidAnimationProfile& profile, std::string* errorMessage)
{
    const HumanoidLocomotionProfile& value = profile.Locomotion;
    if (value.IdleAnimationName.empty() || value.WalkAnimationName.empty() || value.RunAnimationName.empty())
    {
        return SetError(errorMessage, "Animation名は空にできません");
    }
    if (std::isfinite(value.IdleThreshold) == false || std::isfinite(value.WalkThreshold) == false
        || std::isfinite(value.RunThreshold) == false || value.IdleThreshold < 0.0f
        || value.WalkThreshold <= value.IdleThreshold || value.RunThreshold <= value.WalkThreshold)
    {
        return SetError(errorMessage, "Thresholdは 0 <= Idle < Walk < Run を満たす必要があります");
    }
    if (std::isfinite(value.WalkAuthoredMotionSpeed) == false
        || std::isfinite(value.RunAuthoredMotionSpeed) == false
        || value.WalkAuthoredMotionSpeed <= 0.0f
        || value.RunAuthoredMotionSpeed <= value.WalkAuthoredMotionSpeed)
    {
        return SetError(errorMessage, "Authored Motion Speedは 0 < Walk < Run を満たす必要があります");
    }
    return true;
}

const Core::JsonValue* Require(
    const Core::JsonValue& object, const char* key, Core::JsonValue::Type type, std::string* errorMessage)
{
    const Core::JsonValue* value = object.Find(key);
    if (value == nullptr || value->GetType() != type)
    {
        SetError(errorMessage, std::string("Profile項目の欠落または型不一致: ") + key);
        return nullptr;
    }
    return value;
}

bool ReadString(const Core::JsonValue& object, const char* key, std::string& output, std::string* errorMessage)
{
    const Core::JsonValue* value = Require(object, key, Core::JsonValue::Type::String, errorMessage);
    if (value == nullptr) { return false; }
    output = value->GetString();
    return true;
}

bool ReadFloat(const Core::JsonValue& object, const char* key, float& output, std::string* errorMessage)
{
    const Core::JsonValue* value = Require(object, key, Core::JsonValue::Type::Number, errorMessage);
    if (value == nullptr) { return false; }
    const double number = value->GetNumber();
    const float converted = static_cast<float>(number);
    if (std::isfinite(number) == false || std::isfinite(converted) == false)
    {
        return SetError(errorMessage, std::string("Profile数値がfloat範囲外です: ") + key);
    }
    output = converted;
    return true;
}
} // namespace

bool SerializeHumanoidAnimationProfile(
    const HumanoidAnimationProfile& profile, std::string& outText, std::string* errorMessage)
{
    if (errorMessage != nullptr) { errorMessage->clear(); }
    if (ValidateProfile(profile, errorMessage) == false) { return false; }

    const HumanoidLocomotionProfile& value = profile.Locomotion;
    Core::JsonValue::Object locomotion;
    locomotion.emplace("idleAnimation", Core::JsonValue(value.IdleAnimationName));
    locomotion.emplace("walkAnimation", Core::JsonValue(value.WalkAnimationName));
    locomotion.emplace("runAnimation", Core::JsonValue(value.RunAnimationName));
    locomotion.emplace("idleThreshold", Core::JsonValue(static_cast<double>(value.IdleThreshold)));
    locomotion.emplace("walkThreshold", Core::JsonValue(static_cast<double>(value.WalkThreshold)));
    locomotion.emplace("runThreshold", Core::JsonValue(static_cast<double>(value.RunThreshold)));
    locomotion.emplace("walkAuthoredMotionSpeed", Core::JsonValue(static_cast<double>(value.WalkAuthoredMotionSpeed)));
    locomotion.emplace("runAuthoredMotionSpeed", Core::JsonValue(static_cast<double>(value.RunAuthoredMotionSpeed)));
    Core::JsonValue::Object root;
    root.emplace("type", Core::JsonValue(std::string(ProfileType)));
    root.emplace("version", Core::JsonValue(static_cast<double>(CurrentProfileVersion)));
    root.emplace("locomotion", Core::JsonValue(std::move(locomotion)));
    return Core::JsonWriter::Write(Core::JsonValue(std::move(root)), outText, errorMessage);
}

bool DeserializeHumanoidAnimationProfile(
    const std::string& text, HumanoidAnimationProfile& outProfile, std::string* errorMessage)
{
    if (errorMessage != nullptr) { errorMessage->clear(); }
    Core::JsonValue root;
    if (Core::JsonParser::Parse(text, root, errorMessage) == false) { return false; }
    if (root.IsObject() == false)
    {
        return SetError(errorMessage, "Humanoid Animation ProfileのRootはObjectである必要があります");
    }
    const Core::JsonValue* type = Require(root, "type", Core::JsonValue::Type::String, errorMessage);
    const Core::JsonValue* version = Require(root, "version", Core::JsonValue::Type::Number, errorMessage);
    const Core::JsonValue* locomotion = Require(root, "locomotion", Core::JsonValue::Type::Object, errorMessage);
    if (type == nullptr || version == nullptr || locomotion == nullptr) { return false; }
    if (type->GetString() != ProfileType || version->GetNumber() != static_cast<double>(CurrentProfileVersion))
    {
        return SetError(errorMessage, "Humanoid Animation Profileのtypeまたはversionが不正です");
    }

    HumanoidAnimationProfile profile{};
    HumanoidLocomotionProfile& value = profile.Locomotion;
    if (ReadString(*locomotion, "idleAnimation", value.IdleAnimationName, errorMessage) == false
        || ReadString(*locomotion, "walkAnimation", value.WalkAnimationName, errorMessage) == false
        || ReadString(*locomotion, "runAnimation", value.RunAnimationName, errorMessage) == false
        || ReadFloat(*locomotion, "idleThreshold", value.IdleThreshold, errorMessage) == false
        || ReadFloat(*locomotion, "walkThreshold", value.WalkThreshold, errorMessage) == false
        || ReadFloat(*locomotion, "runThreshold", value.RunThreshold, errorMessage) == false
        || ReadFloat(*locomotion, "walkAuthoredMotionSpeed", value.WalkAuthoredMotionSpeed, errorMessage) == false
        || ReadFloat(*locomotion, "runAuthoredMotionSpeed", value.RunAuthoredMotionSpeed, errorMessage) == false
        || ValidateProfile(profile, errorMessage) == false)
    {
        return false;
    }
    // 全検査後にだけ反映し、壊れたAssetによって利用中の設定を失わないようにします。
    outProfile = std::move(profile);
    return true;
}

bool SaveHumanoidAnimationProfile(
    const std::string& filePath, const HumanoidAnimationProfile& profile, std::string* errorMessage)
{
    std::string text;
    if (SerializeHumanoidAnimationProfile(profile, text, errorMessage) == false) { return false; }
    std::ofstream file(filePath, std::ios::binary | std::ios::trunc);
    if (file.is_open() == false) { return SetError(errorMessage, "Profileを開けません: " + filePath); }
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (file.good() == false) { return SetError(errorMessage, "Profileを書き込めません: " + filePath); }
    return true;
}

bool LoadHumanoidAnimationProfile(
    const std::string& filePath, HumanoidAnimationProfile& outProfile, std::string* errorMessage)
{
    std::ifstream file(filePath, std::ios::binary);
    if (file.is_open() == false) { return SetError(errorMessage, "Profileを開けません: " + filePath); }
    std::ostringstream stream;
    stream << file.rdbuf();
    if (file.bad() == true) { return SetError(errorMessage, "Profileを読み取れません: " + filePath); }
    return DeserializeHumanoidAnimationProfile(stream.str(), outProfile, errorMessage);
}
} // namespace Raven
