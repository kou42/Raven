// Raven/Gltf/AnimationImporter.h
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "Raven/Animation/AnimationClip.h"
#include "Raven/Gltf/GltfDocument.h"
#include "Raven/Gltf/JsonValue.h"
#include "Raven/Gltf/SkinImporter.h"

namespace Raven
{
namespace Gltf
{

// ============================================================================
// ImportedAnimationClip
// ============================================================================
// AnimationClip本体はRuntimeからAsset Format依存情報を排除しているため、
// glTF側のanimation index / nameはImporter結果のMetadataとして分離して保持します。
struct ImportedAnimationClip
{
    std::size_t AnimationIndex = InvalidGltfIndex;
    std::string Name;
    AnimationClip Clip;
};

// ============================================================================
// AnimationImporter
// ============================================================================
// glTF animations[]をRaven::AnimationClipへ変換します。
//
// 現段階の責務:
// - sampler.input(SCALAR float)をKey Timeとして読む
// - channel.target.nodeをImportedSkin::NodeToBoneIndexでBoneIndexへ変換する
// - translation / rotation / scaleをTransformAnimationTrackへ格納する
// - RotationはglTFの(x, y, z, w)をRaven::math::Quatへ変換して正規化する
// - Clip Durationは全Channelの最終Key Timeの最大値から決定する
//
// 重要:
// Raven::AnimationClipの現在のSamplerは線形補間を行うため、glTFのSTEP / CUBICSPLINEを
// LINEARとして誤解釈してはいけません。対応実装が入るまでは明示的にImport失敗とします。
class AnimationImporter
{
public:
    static bool LoadFromGlb(
        const std::string& filePath,
        const ImportedSkin& skin,
        std::vector<ImportedAnimationClip>& outClips,
        std::string* errorMessage = nullptr);

    // Testや将来の.gltf対応からも再利用できるよう、JSONとDocumentを受け取る経路を分離します。
    static bool BuildFromJson(
        const JsonValue& root,
        const GltfDocument& document,
        const ImportedSkin& skin,
        std::vector<ImportedAnimationClip>& outClips,
        std::string* errorMessage = nullptr);
};

} // namespace Gltf
} // namespace Raven
