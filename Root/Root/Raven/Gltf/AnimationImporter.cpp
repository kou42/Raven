// Raven/Gltf/AnimationImporter.cpp
#include "Raven/Gltf/AnimationImporter.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "Raven/Gltf/AccessorReader.h"
#include "Raven/Gltf/GlbReader.h"
#include "Raven/Gltf/JsonParser.h"

namespace Raven
{
namespace Gltf
{
namespace
{

struct ParsedAnimationSampler
{
    std::size_t InputAccessorIndex = InvalidGltfIndex;
    std::size_t OutputAccessorIndex = InvalidGltfIndex;
};

struct BoneTrackBuildState
{
    BoneAnimationTrack Track;
    bool Used = false;
    bool HasTranslation = false;
    bool HasRotation = false;
    bool HasScale = false;
};

bool SetError(std::string* errorMessage, const std::string& message)
{
    if (errorMessage != nullptr)
    {
        *errorMessage = message;
    }

    return false;
}

bool ReadSize(const JsonValue& value, std::size_t& outValue)
{
    if (value.IsNumber() == false)
    {
        return false;
    }

    const double number = value.GetNumber();
    if (std::isfinite(number) == false
        || number < 0.0
        || std::floor(number) != number
        || number > static_cast<double>((std::numeric_limits<std::size_t>::max)()))
    {
        return false;
    }

    outValue = static_cast<std::size_t>(number);
    return true;
}

bool ReadRequiredSize(
    const JsonValue& object,
    const char* key,
    std::size_t& outValue,
    const std::string& context,
    std::string* errorMessage)
{
    const JsonValue* value = object.Find(key);
    if (value == nullptr)
    {
        return SetError(errorMessage, context + "." + key + " がありません");
    }
    if (ReadSize(*value, outValue) == false)
    {
        return SetError(errorMessage, context + "." + key + " は0以上の整数である必要があります");
    }

    return true;
}

// ============================================================================
// ReadAnimationTimes
// ============================================================================
// Animation sampler.inputはglTF 2.0仕様上、非normalized FLOAT SCALARです。
// 一般Accessor ReaderへAnimation固有制約を混ぜず、このImporterで型を厳密に限定して読みます。
// GltfDocument::BuildFromJson()ですでにAccessor/BufferViewの基本範囲は検証済みですが、
// Importer単体でも参照先を再確認して不正Assetを黙って受け入れないようにします。
bool ReadAnimationTimes(
    const GltfDocument& document,
    std::size_t accessorIndex,
    std::vector<float>& outTimes,
    const std::string& context,
    std::string* errorMessage)
{
    const std::vector<Accessor>& accessors = document.GetAccessors();
    const std::vector<BufferView>& bufferViews = document.GetBufferViews();
    const std::vector<Buffer>& buffers = document.GetBuffers();
    const std::vector<std::uint8_t>& binaryChunk = document.GetBinaryChunk();

    if (accessorIndex >= accessors.size())
    {
        return SetError(errorMessage, context + " のinput Accessorが範囲外です");
    }

    const Accessor& accessor = accessors[accessorIndex];
    if (accessor.Type != AccessorType::Scalar
        || accessor.Component != ComponentType::Float
        || accessor.Normalized)
    {
        return SetError(errorMessage, context + " のinputは非normalized FLOAT SCALARである必要があります");
    }
    if (accessor.BufferViewIndex == InvalidGltfIndex
        || accessor.BufferViewIndex >= bufferViews.size())
    {
        return SetError(errorMessage, context + " のinput Accessorに有効なBufferViewがありません");
    }

    const BufferView& bufferView = bufferViews[accessor.BufferViewIndex];
    if (bufferView.BufferIndex >= buffers.size())
    {
        return SetError(errorMessage, context + " のinput Buffer indexが範囲外です");
    }

    const Buffer& buffer = buffers[bufferView.BufferIndex];
    if (bufferView.BufferIndex != 0u || buffer.Uri.empty() == false)
    {
        return SetError(errorMessage, context + " の外部URI Bufferは現段階では未対応です");
    }

    const std::size_t stride = bufferView.ByteStride == 0u
        ? sizeof(float)
        : bufferView.ByteStride;

    if (stride < sizeof(float))
    {
        return SetError(errorMessage, context + " のinput strideがfloatより小さいです");
    }
    if (bufferView.ByteOffset > binaryChunk.size())
    {
        return SetError(errorMessage, context + " のBufferView offsetがBIN Chunk範囲外です");
    }
    if (accessor.ByteOffset > binaryChunk.size() - bufferView.ByteOffset)
    {
        return SetError(errorMessage, context + " のAccessor offsetがBIN Chunk範囲外です");
    }

    const std::size_t absoluteOffset = bufferView.ByteOffset + accessor.ByteOffset;
    outTimes.clear();
    outTimes.resize(accessor.Count);

    float previousTime = -1.0f;
    for (std::size_t keyIndex = 0u; keyIndex < accessor.Count; ++keyIndex)
    {
        if (keyIndex > 0u
            && stride > ((std::numeric_limits<std::size_t>::max)() / keyIndex))
        {
            return SetError(errorMessage, context + " のinput offset計算がoverflowしました");
        }

        const std::size_t relativeOffset = stride * keyIndex;
        if (absoluteOffset > binaryChunk.size()
            || relativeOffset > binaryChunk.size() - absoluteOffset
            || sizeof(float) > binaryChunk.size() - absoluteOffset - relativeOffset)
        {
            return SetError(errorMessage, context + " のinput KeyがBIN Chunk範囲外です");
        }

        float time = 0.0f;
        std::memcpy(&time, binaryChunk.data() + absoluteOffset + relativeOffset, sizeof(float));

        // glTF Animation Inputは単調増加かつ0以上です。
        // 同時刻Keyを許可すると補間区間が0になりRuntimeの意味が曖昧になるため拒否します。
        if (std::isfinite(time) == false || time < 0.0f)
        {
            return SetError(errorMessage, context + " のKey Timeは0以上の有限値である必要があります");
        }
        if (keyIndex > 0u && time <= previousTime)
        {
            return SetError(errorMessage, context + " のKey Timeは厳密な昇順である必要があります");
        }

        outTimes[keyIndex] = time;
        previousTime = time;
    }

    return true;
}

bool ParseSamplers(
    const JsonValue& animationValue,
    std::size_t accessorCount,
    std::size_t animationIndex,
    std::vector<ParsedAnimationSampler>& outSamplers,
    std::string* errorMessage)
{
    const std::string animationContext = "animations[" + std::to_string(animationIndex) + "]";
    const JsonValue* samplers = animationValue.Find("samplers");
    if (samplers == nullptr || samplers->IsArray() == false)
    {
        return SetError(errorMessage, animationContext + ".samplers Arrayがありません");
    }

    const JsonValue::Array& samplerArray = samplers->GetArray();
    outSamplers.clear();
    outSamplers.reserve(samplerArray.size());

    for (std::size_t samplerIndex = 0u; samplerIndex < samplerArray.size(); ++samplerIndex)
    {
        const JsonValue& samplerValue = samplerArray[samplerIndex];
        const std::string context = animationContext + ".samplers[" + std::to_string(samplerIndex) + "]";
        if (samplerValue.IsObject() == false)
        {
            return SetError(errorMessage, context + " はObjectである必要があります");
        }

        ParsedAnimationSampler sampler;
        if (ReadRequiredSize(
                samplerValue,
                "input",
                sampler.InputAccessorIndex,
                context,
                errorMessage) == false
            || ReadRequiredSize(
                samplerValue,
                "output",
                sampler.OutputAccessorIndex,
                context,
                errorMessage) == false)
        {
            return false;
        }

        if (sampler.InputAccessorIndex >= accessorCount
            || sampler.OutputAccessorIndex >= accessorCount)
        {
            return SetError(errorMessage, context + " のAccessor indexが範囲外です");
        }

        // glTF既定値はLINEARです。
        // 現在のAnimationClip::Sample()もLerp/Slerpなので、LINEARだけが意味を保ったまま変換できます。
        const JsonValue* interpolation = samplerValue.Find("interpolation");
        if (interpolation != nullptr)
        {
            if (interpolation->IsString() == false)
            {
                return SetError(errorMessage, context + ".interpolation はStringである必要があります");
            }
            if (interpolation->GetString() != "LINEAR")
            {
                return SetError(
                    errorMessage,
                    context + ".interpolation=" + interpolation->GetString()
                        + " は現段階では未対応です。LINEARのみ対応しています");
            }
        }

        outSamplers.emplace_back(sampler);
    }

    return true;
}

bool ValidateOutputAccessor(
    const GltfDocument& document,
    std::size_t accessorIndex,
    AccessorType expectedType,
    std::size_t expectedCount,
    const std::string& context,
    std::string* errorMessage)
{
    const std::vector<Accessor>& accessors = document.GetAccessors();
    if (accessorIndex >= accessors.size())
    {
        return SetError(errorMessage, context + " のoutput Accessorが範囲外です");
    }

    const Accessor& accessor = accessors[accessorIndex];
    if (accessor.Type != expectedType
        || accessor.Component != ComponentType::Float
        || accessor.Normalized)
    {
        return SetError(errorMessage, context + " のoutput Accessor型がAnimation Channel要件と一致しません");
    }
    if (accessor.Count != expectedCount)
    {
        return SetError(errorMessage, context + " のinput/output Key数が一致しません");
    }

    return true;
}

bool ImportVec3Channel(
    const GltfDocument& document,
    const ParsedAnimationSampler& sampler,
    const std::vector<float>& times,
    std::vector<AnimationKeyframe<math::Vec3>>& outKeys,
    const std::string& context,
    std::string* errorMessage)
{
    if (ValidateOutputAccessor(
            document,
            sampler.OutputAccessorIndex,
            AccessorType::Vec3,
            times.size(),
            context,
            errorMessage) == false)
    {
        return false;
    }

    AccessorReader reader(document);
    std::vector<math::Vec3> values;
    if (reader.ReadVec3(sampler.OutputAccessorIndex, values, errorMessage) == false)
    {
        return false;
    }
    if (values.size() != times.size())
    {
        return SetError(errorMessage, context + " のVec3 Key数が一致しません");
    }

    outKeys.clear();
    outKeys.reserve(times.size());
    for (std::size_t keyIndex = 0u; keyIndex < times.size(); ++keyIndex)
    {
        const math::Vec3& value = values[keyIndex];
        if (std::isfinite(value.x) == false
            || std::isfinite(value.y) == false
            || std::isfinite(value.z) == false)
        {
            return SetError(errorMessage, context + " に非有限Vec3 Keyがあります");
        }

        outKeys.emplace_back(AnimationKeyframe<math::Vec3>{ times[keyIndex], value });
    }

    return true;
}

bool ImportRotationChannel(
    const GltfDocument& document,
    const ParsedAnimationSampler& sampler,
    const std::vector<float>& times,
    std::vector<AnimationKeyframe<math::Quat>>& outKeys,
    const std::string& context,
    std::string* errorMessage)
{
    if (ValidateOutputAccessor(
            document,
            sampler.OutputAccessorIndex,
            AccessorType::Vec4,
            times.size(),
            context,
            errorMessage) == false)
    {
        return false;
    }

    AccessorReader reader(document);
    std::vector<math::Vec4> values;
    if (reader.ReadVec4(sampler.OutputAccessorIndex, values, errorMessage) == false)
    {
        return false;
    }
    if (values.size() != times.size())
    {
        return SetError(errorMessage, context + " のQuaternion Key数が一致しません");
    }

    outKeys.clear();
    outKeys.reserve(times.size());
    for (std::size_t keyIndex = 0u; keyIndex < times.size(); ++keyIndex)
    {
        const math::Vec4& value = values[keyIndex];
        if (std::isfinite(value.x) == false
            || std::isfinite(value.y) == false
            || std::isfinite(value.z) == false
            || std::isfinite(value.w) == false)
        {
            return SetError(errorMessage, context + " に非有限Quaternion Keyがあります");
        }

        // glTF Quaternionの格納順は(x, y, z, w)でRaven::math::Quatと一致します。
        // Asset側の丸め誤差で単位長から僅かにずれることがあるため、Import境界で正規化します。
        math::Quat rotation{ value.x, value.y, value.z, value.w };
        const float lengthSq = rotation.LengthSq();
        if (std::isfinite(lengthSq) == false || lengthSq <= 1.0e-12f)
        {
            return SetError(errorMessage, context + " に正規化できないQuaternion Keyがあります");
        }
        rotation.Normalize();

        outKeys.emplace_back(AnimationKeyframe<math::Quat>{ times[keyIndex], rotation });
    }

    return true;
}

bool ImportChannel(
    const JsonValue& channelValue,
    std::size_t channelIndex,
    std::size_t animationIndex,
    const std::vector<ParsedAnimationSampler>& samplers,
    const GltfDocument& document,
    const ImportedSkin& skin,
    std::vector<BoneTrackBuildState>& trackStates,
    float& inOutDuration,
    std::string* errorMessage)
{
    const std::string context = "animations[" + std::to_string(animationIndex)
        + "].channels[" + std::to_string(channelIndex) + "]";

    if (channelValue.IsObject() == false)
    {
        return SetError(errorMessage, context + " はObjectである必要があります");
    }

    std::size_t samplerIndex = InvalidGltfIndex;
    if (ReadRequiredSize(channelValue, "sampler", samplerIndex, context, errorMessage) == false)
    {
        return false;
    }
    if (samplerIndex >= samplers.size())
    {
        return SetError(errorMessage, context + ".sampler が範囲外です");
    }

    const JsonValue* target = channelValue.Find("target");
    if (target == nullptr || target->IsObject() == false)
    {
        return SetError(errorMessage, context + ".target Objectがありません");
    }

    std::size_t nodeIndex = InvalidGltfIndex;
    if (ReadRequiredSize(*target, "node", nodeIndex, context + ".target", errorMessage) == false)
    {
        return false;
    }
    if (nodeIndex >= skin.NodeToBoneIndex.size())
    {
        return SetError(errorMessage, context + ".target.node がNode範囲外です");
    }

    const JsonValue* pathValue = target->Find("path");
    if (pathValue == nullptr || pathValue->IsString() == false)
    {
        return SetError(errorMessage, context + ".target.path Stringがありません");
    }

    const BoneIndex boneIndex = skin.NodeToBoneIndex[nodeIndex];
    if (boneIndex == InvalidBoneIndex)
    {
        // 同じglTF animationにはCamera/Mesh NodeなどSkin外NodeのChannelも含められます。
        // このImporterはSkeletal Animation専用なので、それらはAnimationClipへ混ぜず無視します。
        // Root Motion用の非Joint Armature Node取り込みはCharacter Controller段階で別途扱います。
        return true;
    }
    if (skin.SkeletonData.IsValidBoneIndex(boneIndex) == false)
    {
        return SetError(errorMessage, context + " のNodeToBoneIndexがSkeleton範囲外です");
    }

    const ParsedAnimationSampler& sampler = samplers[samplerIndex];
    std::vector<float> times;
    if (ReadAnimationTimes(
            document,
            sampler.InputAccessorIndex,
            times,
            context,
            errorMessage) == false)
    {
        return false;
    }
    if (times.empty())
    {
        return SetError(errorMessage, context + " のAnimation Keyが空です");
    }

    const std::size_t boneSlot = static_cast<std::size_t>(boneIndex);
    if (boneSlot >= trackStates.size())
    {
        return SetError(errorMessage, context + " のBoneIndexがTrack配列範囲外です");
    }

    BoneTrackBuildState& state = trackStates[boneSlot];
    state.Used = true;
    state.Track.Bone = boneIndex;

    const std::string& path = pathValue->GetString();
    if (path == "translation")
    {
        if (state.HasTranslation)
        {
            return SetError(errorMessage, context + " で同一Boneのtranslation Channelが重複しています");
        }
        if (ImportVec3Channel(
                document,
                sampler,
                times,
                state.Track.Transform.PositionKeys,
                context,
                errorMessage) == false)
        {
            return false;
        }
        state.HasTranslation = true;
    }
    else if (path == "rotation")
    {
        if (state.HasRotation)
        {
            return SetError(errorMessage, context + " で同一Boneのrotation Channelが重複しています");
        }
        if (ImportRotationChannel(
                document,
                sampler,
                times,
                state.Track.Transform.RotationKeys,
                context,
                errorMessage) == false)
        {
            return false;
        }
        state.HasRotation = true;
    }
    else if (path == "scale")
    {
        if (state.HasScale)
        {
            return SetError(errorMessage, context + " で同一Boneのscale Channelが重複しています");
        }
        if (ImportVec3Channel(
                document,
                sampler,
                times,
                state.Track.Transform.ScaleKeys,
                context,
                errorMessage) == false)
        {
            return false;
        }
        state.HasScale = true;
    }
    else
    {
        return SetError(errorMessage, context + ".target.path=" + path + " はSkeletal Animationでは未対応です");
    }

    inOutDuration = (std::max)(inOutDuration, times.back());
    return true;
}

bool ImportAnimation(
    const JsonValue& animationValue,
    std::size_t animationIndex,
    const GltfDocument& document,
    const ImportedSkin& skin,
    ImportedAnimationClip& outClip,
    std::string* errorMessage)
{
    const std::string context = "animations[" + std::to_string(animationIndex) + "]";
    if (animationValue.IsObject() == false)
    {
        return SetError(errorMessage, context + " はObjectである必要があります");
    }

    std::vector<ParsedAnimationSampler> samplers;
    if (ParseSamplers(
            animationValue,
            document.GetAccessors().size(),
            animationIndex,
            samplers,
            errorMessage) == false)
    {
        return false;
    }

    const JsonValue* channels = animationValue.Find("channels");
    if (channels == nullptr || channels->IsArray() == false)
    {
        return SetError(errorMessage, context + ".channels Arrayがありません");
    }

    ImportedAnimationClip imported;
    imported.AnimationIndex = animationIndex;

    const JsonValue* name = animationValue.Find("name");
    if (name != nullptr)
    {
        if (name->IsString() == false)
        {
            return SetError(errorMessage, context + ".name はStringである必要があります");
        }
        imported.Name = name->GetString();
    }
    if (imported.Name.empty())
    {
        imported.Name = "Animation_" + std::to_string(animationIndex);
    }

    std::vector<BoneTrackBuildState> trackStates(skin.SkeletonData.GetBoneCount());
    float duration = 0.0f;

    const JsonValue::Array& channelArray = channels->GetArray();
    for (std::size_t channelIndex = 0u; channelIndex < channelArray.size(); ++channelIndex)
    {
        if (ImportChannel(
                channelArray[channelIndex],
                channelIndex,
                animationIndex,
                samplers,
                document,
                skin,
                trackStates,
                duration,
                errorMessage) == false)
        {
            return false;
        }
    }

    imported.Clip.SetDuration(duration);

    for (BoneTrackBuildState& state : trackStates)
    {
        if (state.Used == false)
        {
            continue;
        }

        if (imported.Clip.AddBoneTrack(std::move(state.Track)) == false)
        {
            return SetError(errorMessage, context + " のBone Track追加に失敗しました");
        }
    }

    outClip = std::move(imported);
    return true;
}

} // namespace

bool AnimationImporter::LoadFromGlb(
    const std::string& filePath,
    const ImportedSkin& skin,
    std::vector<ImportedAnimationClip>& outClips,
    std::string* errorMessage)
{
    if (errorMessage != nullptr)
    {
        errorMessage->clear();
    }

    GlbData glbData;
    if (GlbReader::ReadFromFile(filePath, glbData, errorMessage) == false)
    {
        return false;
    }

    JsonValue root;
    if (JsonParser::Parse(glbData.JsonText, root, errorMessage) == false)
    {
        return false;
    }

    GltfDocument document;
    if (GltfDocument::BuildFromJson(
            root,
            std::move(glbData.BinaryChunk),
            document,
            errorMessage) == false)
    {
        return false;
    }

    return BuildFromJson(root, document, skin, outClips, errorMessage);
}

bool AnimationImporter::BuildFromJson(
    const JsonValue& root,
    const GltfDocument& document,
    const ImportedSkin& skin,
    std::vector<ImportedAnimationClip>& outClips,
    std::string* errorMessage)
{
    if (errorMessage != nullptr)
    {
        errorMessage->clear();
    }

    if (root.IsObject() == false)
    {
        return SetError(errorMessage, "glTF RootはObjectである必要があります");
    }
    if (skin.NodeToBoneIndex.empty())
    {
        return SetError(errorMessage, "Animation ImportにはImportedSkin::NodeToBoneIndexが必要です");
    }

    const JsonValue* animations = root.Find("animations");
    if (animations == nullptr)
    {
        outClips.clear();
        return true;
    }
    if (animations->IsArray() == false)
    {
        return SetError(errorMessage, "glTF.animationsはArrayである必要があります");
    }

    const JsonValue::Array& animationArray = animations->GetArray();
    std::vector<ImportedAnimationClip> importedClips;
    importedClips.reserve(animationArray.size());

    for (std::size_t animationIndex = 0u; animationIndex < animationArray.size(); ++animationIndex)
    {
        ImportedAnimationClip clip;
        if (ImportAnimation(
                animationArray[animationIndex],
                animationIndex,
                document,
                skin,
                clip,
                errorMessage) == false)
        {
            return false;
        }

        importedClips.emplace_back(std::move(clip));
    }

    outClips = std::move(importedClips);
    return true;
}

} // namespace Gltf
} // namespace Raven
