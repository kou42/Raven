#pragma once

#include "Raven/Animation/MotionDatabase.h"
#include "Raven/Animation/PoseInertializer.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <vector>

namespace Raven
{

struct MotionMatcherConfig
{
    MotionSearchWeights SearchWeights{};
    float MinimumSwitchInterval = 0.15f;
    float StayBonus = 0.0f;
    float SwitchCost = 0.0f;
    bool EnableInertialization = true;
    PoseInertializerConfig Inertialization{};
    bool Loop = true;
};

// 検索CostをFeature種別ごとに分解した診断値です。
// 各値はSearchWeights適用後のCostなので、合計値は実際の検索Costと一致します。
struct MotionSearchCostBreakdown
{
    float PosePosition = 0.0f;
    float PoseVelocity = 0.0f;
    float TrajectoryPosition = 0.0f;
    float TrajectoryDirection = 0.0f;

    float GetTotal() const
    {
        return PosePosition + PoseVelocity + TrajectoryPosition + TrajectoryDirection;
    }
};

// 直近のDatabase検索でCostが小さかった候補を、検索本体と同じCost空間で保持します。
// EditorがDatabase全体を再評価せず「なぜこのFrameが候補になったか」を確認するための診断値です。
struct MotionSearchCandidateDebugInfo
{
    std::size_t FrameIndex = std::numeric_limits<std::size_t>::max();
    std::uint32_t ClipIndex = 0u;
    float ClipTime = 0.0f;
    float Cost = std::numeric_limits<float>::max();
    MotionSearchCostBreakdown CostBreakdown{};
    std::vector<MotionTrajectoryPoint> Trajectory;
};

class MotionMatcher
{
public:
    static constexpr std::size_t SearchCandidateDebugCount = 5u;

    void SetDatabase(std::shared_ptr<const MotionDatabase> database);
    const std::shared_ptr<const MotionDatabase>& GetDatabase() const { return m_Database; }

    void SetConfig(const MotionMatcherConfig& config) { m_Config = config; }
    const MotionMatcherConfig& GetConfig() const { return m_Config; }

    // Databaseの標準偏差を使ってSemantic Weightを1/sigma^2補正し、SearchWeightsへ設定します。
    // FindBestMatchとContinuation Costは同じm_Config.SearchWeightsを参照するため、
    // StayBonus / SwitchCostを含む切替判断まで同じ正規化Cost空間へ揃います。
    bool SetNormalizedSearchWeights(const MotionSearchWeights& semanticWeights);

    void Reset();

    bool Update(
        const Skeleton& skeleton,
        const MotionSearchQuery& query,
        float deltaTime,
        SkeletonPose& outPose);

    bool HasSelection() const { return m_HasSelection; }
    std::size_t GetSelectedFrameIndex() const { return m_SelectedFrameIndex; }
    std::uint32_t GetCurrentClipIndex() const { return m_CurrentClipIndex; }
    float GetCurrentTime() const { return m_CurrentTime; }
    float GetLastSearchCost() const { return m_LastSearchCost; }
    const std::vector<MotionSearchCandidateDebugInfo>& GetLastSearchCandidates() const
    {
        return m_LastSearchCandidates;
    }
    bool IsInertializing() const { return m_Inertializer.IsActive(); }
    float GetInertializationElapsedTime() const { return m_Inertializer.GetElapsedTime(); }

    bool GetInertializationBoneDebugInfo(
        BoneIndex boneIndex,
        PoseInertializerBoneDebugInfo& outInfo) const
    {
        return m_Inertializer.GetBoneDebugInfo(boneIndex, outInfo);
    }

private:
    bool SelectFrame(const MotionSearchResult& searchResult);
    bool AdvanceCurrentTime(float deltaTime);

    bool SearchDatabase(
        const MotionSearchQuery& query,
        MotionSearchResult& outBestResult);

    bool SamplePreviousTargetPose(
        const Skeleton& skeleton,
        const AnimationClip& clip,
        float velocityDeltaTime,
        SkeletonPose& outPose) const;

    bool FindContinuationFrame(std::size_t& outFrameIndex) const;

    // Cost合計とFeature別内訳を同じ1回の計算から生成します。
    // 検索判定とDebug表示で式を二重管理しないことが重要です。
    bool CalculateFrameCost(
        const MotionSearchQuery& query,
        std::size_t frameIndex,
        float& outCost,
        MotionSearchCostBreakdown* outBreakdown = nullptr) const;

private:
    std::shared_ptr<const MotionDatabase> m_Database;
    MotionMatcherConfig m_Config{};
    PoseInertializer m_Inertializer{};

    SkeletonPose m_PreviousOutputPose{};
    SkeletonPose m_LastOutputPose{};

    std::size_t m_SelectedFrameIndex = std::numeric_limits<std::size_t>::max();
    std::uint32_t m_CurrentClipIndex = 0;
    float m_CurrentTime = 0.0f;
    float m_TimeSinceSwitch = 0.0f;
    float m_LastSearchCost = std::numeric_limits<float>::max();
    std::vector<MotionSearchCandidateDebugInfo> m_LastSearchCandidates;

    float m_LastOutputDeltaTime = 0.0f;

    bool m_HasSelection = false;
    bool m_HasPreviousOutputPose = false;
    bool m_HasLastOutputPose = false;
};

} // namespace Raven
