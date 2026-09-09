#pragma once

#include "Raven/Animation/AnimationClip.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace Raven
{

// ============================================================================
// MotionFrame
// ============================================================================
// Motion Database内の1サンプルを「どのAnimationClipの何秒地点か」で表します。
//
// この段階ではPose / Trajectory Featureを保持しません。
// まずClip群を一定周期の検索候補へ展開する責務だけを固定し、Feature生成・検索ロジックは
// 後続実装でこのFrameへ追加できるよう、元Clipへ戻れる最小情報だけを保持します。
struct MotionFrame
{
    std::uint32_t ClipIndex = 0;
    float Time = 0.0f;
};

// ============================================================================
// MotionDatabase
// ============================================================================
// 複数のAnimationClipをMotion Matching用の固定周期サンプル列へ変換するAsset側データです。
//
// AnimationClip自身は再生状態を持たない既存設計を維持し、MotionDatabaseもCurrent Frameなどの
// Runtime状態を持ちません。将来のMotion Matcher / Animatorが検索結果のFrameを選び、
// ClipIndex + TimeからAnimationClip::Sample()を呼び出す構成を想定しています。
class MotionDatabase
{
public:
    // nullptrや同一Clipの重複登録は、検索候補の無効化・重複化を防ぐため拒否します。
    bool AddClip(std::shared_ptr<AnimationClip> clip);

    void Clear();

    // 登録済みClipをsampleRate HzでMotionFrame列へ展開します。
    // sampleRate <= 0、Clip未登録、Duration <= 0のClipを含む場合はfalseを返し、
    // 部分的なDatabaseを残さないようFrame列を空にします。
    //
    // 各Clipは [0, Duration) を一定間隔でサンプルします。
    // Duration地点はLoop Clipでは0秒地点と重複しやすく、Motion Matching候補として
    // 二重登録されるのを避けるため、この基盤段階では終端を含めません。
    bool Build(float sampleRate);

    std::size_t GetClipCount() const { return m_Clips.size(); }
    std::size_t GetFrameCount() const { return m_Frames.size(); }
    float GetSampleRate() const { return m_SampleRate; }

    const std::shared_ptr<AnimationClip>& GetClip(std::size_t clipIndex) const;
    const MotionFrame* GetFrame(std::size_t frameIndex) const;

    const std::vector<MotionFrame>& GetFrames() const { return m_Frames; }

private:
    std::vector<std::shared_ptr<AnimationClip>> m_Clips;
    std::vector<MotionFrame> m_Frames;
    float m_SampleRate = 0.0f;
};

} // namespace Raven
