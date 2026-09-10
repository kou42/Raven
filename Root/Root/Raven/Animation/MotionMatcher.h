#pragma once

#include "Raven/Animation/MotionDatabase.h"

#include <cstddef>
#include <memory>

namespace Raven
{

// ============================================================================
// MotionMatcherConfig
// ============================================================================
// Motion Matching専用のRuntime設定です。
// 既存AnimatorのLoop / CrossFade設定とは独立させ、従来Animation経路へ影響させません。
struct MotionMatcherConfig
{
    MotionSearchWeights SearchWeights{};

    // 最良候補が毎Frame変化した場合でも短時間に連続切替しないための最低保持時間です。
    // 0なら検索結果に応じて毎Frame切替可能です。
    float MinimumSwitchInterval = 0.15f;

    // 現段階ではLocomotion Databaseを主対象とするため、選択Clipの終端でLoopできます。
    // AnimationClip自体へLoop状態は追加せず、再生InstanceであるMotionMatcherが保持します。
    bool Loop = true;
};

// ============================================================================
// MotionMatcher
// ============================================================================
// MotionDatabaseの検索結果から再生するClip / Timeを選び、SkeletonPoseを評価するRuntime Stateです。
//
// 既存Animator / BlendTree / StateMachineを置き換えず、Motion Matchingを選択したCharacterだけが
// このRuntimeを使用します。Inertializationは後続段階で出力Poseの直前へ追加します。
class MotionMatcher
{
public:
    void SetDatabase(std::shared_ptr<const MotionDatabase> database);
    const std::shared_ptr<const MotionDatabase>& GetDatabase() const { return m_Database; }

    void SetConfig(const MotionMatcherConfig& config) { m_Config = config; }
    const MotionMatcherConfig& GetConfig() const { return m_Config; }

    void Reset();

    // Queryから最良候補を検索し、必要ならClipを切り替えた後でPoseを評価します。
    // 初回Updateでは必ず検索結果のFrameから開始します。
    bool Update(
        const Skeleton& skeleton,
        const MotionSearchQuery& query,
        float deltaTime,
        SkeletonPose& outPose);

    bool HasSelection() const { return m_HasSelection; }
    std::size_t GetCurrentFrameIndex() const { return m_CurrentFrameIndex; }
    std::uint32_t GetCurrentClipIndex() const { return m_CurrentClipIndex; }
    float GetCurrentTime() const { return m_CurrentTime; }
    float GetLastSearchCost() const { return m_LastSearchCost; }

private:
    bool SelectFrame(const MotionSearchResult& searchResult);
    bool AdvanceCurrentTime(float deltaTime);

private:
    std::shared_ptr<const MotionDatabase> m_Database;
    MotionMatcherConfig m_Config{};

    std::size_t m_CurrentFrameIndex = std::numeric_limits<std::size_t>::max();
    std::uint32_t m_CurrentClipIndex = 0;
    float m_CurrentTime = 0.0f;
    float m_TimeSinceSwitch = 0.0f;
    float m_LastSearchCost = std::numeric_limits<float>::max();
    bool m_HasSelection = false;
};

} // namespace Raven
