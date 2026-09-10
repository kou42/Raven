#pragma once

#include "Raven/Animation/MotionDatabase.h"
#include "Raven/Animation/PoseInertializer.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>

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

class MotionMatcher
{
public:
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
    bool IsInertializing() const { return m_Inertializer.IsActive(); }

private:
    bool SelectFrame(const MotionSearchResult& searchResult);
    bool AdvanceCurrentTime(float deltaTime);
    bool FindContinuationFrame(std::size_t& outFrameIndex) const;
    bool CalculateFrameCost(
        const MotionSearchQuery& query,
        std::size_t frameIndex,
        float& outCost) const;

private:
    std::shared_ptr<const MotionDatabase> m_Database;
    MotionMatcherConfig m_Config{};
    PoseInertializer m_Inertializer{};
    SkeletonPose m_LastOutputPose{};

    std::size_t m_SelectedFrameIndex = std::numeric_limits<std::size_t>::max();
    std::uint32_t m_CurrentClipIndex = 0;
    float m_CurrentTime = 0.0f;
    float m_TimeSinceSwitch = 0.0f;
    float m_LastSearchCost = std::numeric_limits<float>::max();
    bool m_HasSelection = false;
    bool m_HasLastOutputPose = false;
};

} // namespace Raven
