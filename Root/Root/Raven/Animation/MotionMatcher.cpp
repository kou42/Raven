#include "Raven/Animation/MotionMatcher.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace Raven
{
namespace
{

double LengthSquared(const math::Vec3& value)
{
    return static_cast<double>(value.x) * static_cast<double>(value.x) +
        static_cast<double>(value.y) * static_cast<double>(value.y) +
        static_cast<double>(value.z) * static_cast<double>(value.z);
}

} // namespace

void MotionMatcher::SetDatabase(std::shared_ptr<const MotionDatabase> database)
{
    if (m_Database.get() == database.get())
    {
        return;
    }

    m_Database = std::move(database);
    Reset();
}

void MotionMatcher::Reset()
{
    m_Inertializer.Reset();
    m_PreviousOutputPose = SkeletonPose{};
    m_LastOutputPose = SkeletonPose{};

    m_SelectedFrameIndex = std::numeric_limits<std::size_t>::max();
    m_CurrentClipIndex = 0;
    m_CurrentTime = 0.0f;
    m_TimeSinceSwitch = 0.0f;
    m_LastSearchCost = std::numeric_limits<float>::max();
    m_LastSearchCandidates.clear();
    m_LastOutputDeltaTime = 0.0f;
    m_HasSelection = false;
    m_HasPreviousOutputPose = false;
    m_HasLastOutputPose = false;
}

bool MotionMatcher::Update(
    const Skeleton& skeleton,
    const MotionSearchQuery& query,
    float deltaTime,
    SkeletonPose& outPose)
{
    if (m_Database == nullptr ||
        deltaTime < 0.0f || std::isfinite(deltaTime) == false ||
        m_Config.MinimumSwitchInterval < 0.0f ||
        std::isfinite(m_Config.MinimumSwitchInterval) == false ||
        m_Config.StayBonus < 0.0f || std::isfinite(m_Config.StayBonus) == false ||
        m_Config.SwitchCost < 0.0f || std::isfinite(m_Config.SwitchCost) == false)
    {
        return false;
    }

    if (m_HasSelection == true)
    {
        if (AdvanceCurrentTime(deltaTime) == false)
        {
            return false;
        }

        m_TimeSinceSwitch += deltaTime;
    }

    const bool shouldSearch =
        m_HasSelection == false ||
        m_TimeSinceSwitch >= m_Config.MinimumSwitchInterval;

    bool switchedThisFrame = false;

    if (shouldSearch == true)
    {
        MotionSearchResult searchResult{};
        if (SearchDatabase(query, searchResult) == false)
        {
            return false;
        }

        m_LastSearchCost = searchResult.Cost;
        bool shouldSwitch = m_HasSelection == false;

        if (m_HasSelection == true)
        {
            const MotionFrame* candidateFrame = m_Database->GetFrame(searchResult.FrameIndex);
            if (candidateFrame == nullptr)
            {
                return false;
            }

            const float sampleRate = m_Database->GetSampleRate();
            const float continuityTolerance =
                (sampleRate > 0.0f) ? (1.5f / sampleRate) : 0.0f;

            const bool sameClip = candidateFrame->ClipIndex == m_CurrentClipIndex;
            float candidateTimeDistance = std::fabs(candidateFrame->Time - m_CurrentTime);

            if (sameClip == true && m_Config.Loop == true)
            {
                const std::shared_ptr<AnimationClip>& currentClip =
                    m_Database->GetClip(static_cast<std::size_t>(m_CurrentClipIndex));
                if (currentClip == nullptr || currentClip->GetDuration() <= 0.0f)
                {
                    return false;
                }

                candidateTimeDistance = std::min(
                    candidateTimeDistance,
                    currentClip->GetDuration() - candidateTimeDistance);
            }

            const bool nearCurrentTime = candidateTimeDistance <= continuityTolerance;

            if (sameClip == false || nearCurrentTime == false)
            {
                shouldSwitch = true;

                std::size_t continuationFrameIndex = std::numeric_limits<std::size_t>::max();
                if (FindContinuationFrame(continuationFrameIndex) == false)
                {
                    return false;
                }

                float continuationCost = 0.0f;
                if (CalculateFrameCost(query, continuationFrameIndex, continuationCost) == false)
                {
                    return false;
                }

                const double adjustedContinuationCost = std::max(
                    0.0,
                    static_cast<double>(continuationCost) - static_cast<double>(m_Config.StayBonus));
                const double adjustedCandidateCost =
                    static_cast<double>(searchResult.Cost) + static_cast<double>(m_Config.SwitchCost);

                if (adjustedContinuationCost <= adjustedCandidateCost)
                {
                    shouldSwitch = false;
                }
            }
        }

        if (shouldSwitch == true)
        {
            if (SelectFrame(searchResult) == false)
            {
                return false;
            }

            switchedThisFrame = true;
        }
    }

    const std::shared_ptr<AnimationClip>& clip =
        m_Database->GetClip(static_cast<std::size_t>(m_CurrentClipIndex));
    if (clip == nullptr)
    {
        return false;
    }

    SkeletonPose targetPose;
    if (clip->Sample(skeleton, m_CurrentTime, targetPose) == false)
    {
        return false;
    }

    if (m_Config.EnableInertialization == true)
    {
        m_Inertializer.SetConfig(m_Config.Inertialization);

        if (switchedThisFrame == true && m_HasLastOutputPose == true)
        {
            bool beganWithVelocity = false;

            if (m_HasPreviousOutputPose == true &&
                m_LastOutputDeltaTime > 0.0f &&
                std::isfinite(m_LastOutputDeltaTime) == true)
            {
                SkeletonPose previousTargetPose;
                if (SamplePreviousTargetPose(
                        skeleton,
                        *clip,
                        m_LastOutputDeltaTime,
                        previousTargetPose) == false)
                {
                    return false;
                }

                if (m_Inertializer.Begin(
                        skeleton,
                        m_PreviousOutputPose,
                        m_LastOutputPose,
                        previousTargetPose,
                        targetPose,
                        m_LastOutputDeltaTime) == false)
                {
                    return false;
                }

                beganWithVelocity = true;
            }

            if (beganWithVelocity == false)
            {
                if (m_Inertializer.Begin(skeleton, m_LastOutputPose, targetPose) == false)
                {
                    return false;
                }
            }
        }
        else if (switchedThisFrame == true)
        {
            m_Inertializer.Reset();
        }

        const float inertialDeltaTime = switchedThisFrame ? 0.0f : deltaTime;
        if (m_Inertializer.Apply(skeleton, targetPose, inertialDeltaTime, outPose) == false)
        {
            return false;
        }
    }
    else
    {
        m_Inertializer.Reset();
        outPose = targetPose;
    }

    if (m_HasLastOutputPose == true)
    {
        m_PreviousOutputPose = m_LastOutputPose;
        m_HasPreviousOutputPose = true;
    }

    m_LastOutputPose = outPose;
    m_LastOutputDeltaTime = deltaTime;
    m_HasLastOutputPose = true;
    return true;
}

bool MotionMatcher::SearchDatabase(
    const MotionSearchQuery& query,
    MotionSearchResult& outBestResult)
{
    outBestResult = MotionSearchResult{};
    m_LastSearchCandidates.clear();

    if (m_Database == nullptr ||
        (query.PoseFeatures.empty() == true && query.Trajectory.empty() == true))
    {
        return false;
    }

    m_LastSearchCandidates.reserve(SearchCandidateDebugCount);

    for (std::size_t frameIndex = 0u; frameIndex < m_Database->GetFrameCount(); ++frameIndex)
    {
        float cost = 0.0f;
        MotionSearchCostBreakdown costBreakdown{};
        if (CalculateFrameCost(query, frameIndex, cost, &costBreakdown) == false)
        {
            continue;
        }

        const MotionFrame* frame = m_Database->GetFrame(frameIndex);
        if (frame == nullptr)
        {
            continue;
        }

        MotionSearchCandidateDebugInfo candidate{};
        candidate.FrameIndex = frameIndex;
        candidate.ClipIndex = frame->ClipIndex;
        candidate.ClipTime = frame->Time;
        candidate.Cost = cost;
        candidate.CostBreakdown = costBreakdown;
        candidate.Trajectory = frame->Trajectory;

        const auto insertPosition = std::lower_bound(
            m_LastSearchCandidates.begin(),
            m_LastSearchCandidates.end(),
            candidate.Cost,
            [](const MotionSearchCandidateDebugInfo& existing, float candidateCost)
            {
                return existing.Cost < candidateCost;
            });

        if (m_LastSearchCandidates.size() < SearchCandidateDebugCount ||
            insertPosition != m_LastSearchCandidates.end())
        {
            m_LastSearchCandidates.insert(insertPosition, std::move(candidate));
            if (m_LastSearchCandidates.size() > SearchCandidateDebugCount)
            {
                m_LastSearchCandidates.pop_back();
            }
        }
    }

    if (m_LastSearchCandidates.empty() == true)
    {
        return false;
    }

    outBestResult.FrameIndex = m_LastSearchCandidates.front().FrameIndex;
    outBestResult.Cost = m_LastSearchCandidates.front().Cost;
    return true;
}

bool MotionMatcher::SelectFrame(const MotionSearchResult& searchResult)
{
    if (m_Database == nullptr || searchResult.IsValid() == false)
    {
        return false;
    }

    const MotionFrame* frame = m_Database->GetFrame(searchResult.FrameIndex);
    if (frame == nullptr)
    {
        return false;
    }

    const std::shared_ptr<AnimationClip>& clip =
        m_Database->GetClip(static_cast<std::size_t>(frame->ClipIndex));
    if (clip == nullptr)
    {
        return false;
    }

    m_SelectedFrameIndex = searchResult.FrameIndex;
    m_CurrentClipIndex = frame->ClipIndex;
    m_CurrentTime = frame->Time;
    m_TimeSinceSwitch = 0.0f;
    m_HasSelection = true;
    return true;
}

bool MotionMatcher::AdvanceCurrentTime(float deltaTime)
{
    if (m_Database == nullptr || m_HasSelection == false)
    {
        return false;
    }

    const std::shared_ptr<AnimationClip>& clip =
        m_Database->GetClip(static_cast<std::size_t>(m_CurrentClipIndex));
    if (clip == nullptr)
    {
        return false;
    }

    const float duration = clip->GetDuration();
    if (duration <= 0.0f || std::isfinite(duration) == false)
    {
        return false;
    }

    m_CurrentTime += deltaTime;

    if (m_Config.Loop == true)
    {
        if (m_CurrentTime >= duration)
        {
            m_CurrentTime = std::fmod(m_CurrentTime, duration);
        }
    }
    else if (m_CurrentTime > duration)
    {
        m_CurrentTime = duration;
    }

    return true;
}

bool MotionMatcher::SamplePreviousTargetPose(
    const Skeleton& skeleton,
    const AnimationClip& clip,
    float velocityDeltaTime,
    SkeletonPose& outPose) const
{
    if (velocityDeltaTime <= 0.0f || std::isfinite(velocityDeltaTime) == false)
    {
        return false;
    }

    const float duration = clip.GetDuration();
    if (duration <= 0.0f || std::isfinite(duration) == false)
    {
        return false;
    }

    float previousTime = m_CurrentTime - velocityDeltaTime;

    if (m_Config.Loop == true)
    {
        previousTime = std::fmod(previousTime, duration);
        if (previousTime < 0.0f)
        {
            previousTime += duration;
        }
    }
    else
    {
        previousTime = std::max(0.0f, previousTime);
    }

    return clip.Sample(skeleton, previousTime, outPose);
}

bool MotionMatcher::FindContinuationFrame(std::size_t& outFrameIndex) const
{
    outFrameIndex = std::numeric_limits<std::size_t>::max();

    if (m_Database == nullptr || m_HasSelection == false)
    {
        return false;
    }

    const std::shared_ptr<AnimationClip>& clip =
        m_Database->GetClip(static_cast<std::size_t>(m_CurrentClipIndex));
    if (clip == nullptr)
    {
        return false;
    }

    const float duration = clip->GetDuration();
    if (duration <= 0.0f || std::isfinite(duration) == false)
    {
        return false;
    }

    float bestTimeDistance = std::numeric_limits<float>::max();

    for (std::size_t frameIndex = 0; frameIndex < m_Database->GetFrameCount(); ++frameIndex)
    {
        const MotionFrame* frame = m_Database->GetFrame(frameIndex);
        if (frame == nullptr || frame->ClipIndex != m_CurrentClipIndex)
        {
            continue;
        }

        float timeDistance = std::fabs(frame->Time - m_CurrentTime);
        if (m_Config.Loop == true)
        {
            timeDistance = std::min(timeDistance, duration - timeDistance);
        }

        if (timeDistance < bestTimeDistance)
        {
            bestTimeDistance = timeDistance;
            outFrameIndex = frameIndex;
        }
    }

    return outFrameIndex != std::numeric_limits<std::size_t>::max();
}

bool MotionMatcher::CalculateFrameCost(
    const MotionSearchQuery& query,
    std::size_t frameIndex,
    float& outCost,
    MotionSearchCostBreakdown* outBreakdown) const
{
    outCost = std::numeric_limits<float>::max();
    if (outBreakdown != nullptr)
    {
        *outBreakdown = MotionSearchCostBreakdown{};
    }

    if (m_Database == nullptr)
    {
        return false;
    }

    const MotionFrame* frame = m_Database->GetFrame(frameIndex);
    if (frame == nullptr ||
        frame->PoseFeatures.size() != query.PoseFeatures.size() ||
        frame->Trajectory.size() != query.Trajectory.size())
    {
        return false;
    }

    const MotionSearchWeights& weights = m_Config.SearchWeights;
    if (weights.PosePosition < 0.0f || std::isfinite(weights.PosePosition) == false ||
        weights.PoseVelocity < 0.0f || std::isfinite(weights.PoseVelocity) == false ||
        weights.TrajectoryPosition < 0.0f || std::isfinite(weights.TrajectoryPosition) == false ||
        weights.TrajectoryDirection < 0.0f || std::isfinite(weights.TrajectoryDirection) == false)
    {
        return false;
    }

    double posePositionCost = 0.0;
    double poseVelocityCost = 0.0;
    double trajectoryPositionCost = 0.0;
    double trajectoryDirectionCost = 0.0;

    for (std::size_t i = 0; i < frame->PoseFeatures.size(); ++i)
    {
        if (frame->PoseFeatures[i].Bone != query.PoseFeatures[i].Bone)
        {
            return false;
        }

        const math::Vec3 positionDelta =
            frame->PoseFeatures[i].Position - query.PoseFeatures[i].Position;
        const math::Vec3 velocityDelta =
            frame->PoseFeatures[i].Velocity - query.PoseFeatures[i].Velocity;

        posePositionCost += static_cast<double>(weights.PosePosition) * LengthSquared(positionDelta);
        poseVelocityCost += static_cast<double>(weights.PoseVelocity) * LengthSquared(velocityDelta);
    }

    for (std::size_t i = 0; i < frame->Trajectory.size(); ++i)
    {
        if (std::fabs(frame->Trajectory[i].TimeOffset - query.Trajectory[i].TimeOffset) > 1.0e-4f)
        {
            return false;
        }

        const math::Vec3 positionDelta =
            frame->Trajectory[i].Position - query.Trajectory[i].Position;
        const math::Vec3 directionDelta =
            frame->Trajectory[i].Direction - query.Trajectory[i].Direction;

        trajectoryPositionCost +=
            static_cast<double>(weights.TrajectoryPosition) * LengthSquared(positionDelta);
        trajectoryDirectionCost +=
            static_cast<double>(weights.TrajectoryDirection) * LengthSquared(directionDelta);
    }

    const double cost =
        posePositionCost +
        poseVelocityCost +
        trajectoryPositionCost +
        trajectoryDirectionCost;

    if (std::isfinite(cost) == false ||
        cost > static_cast<double>(std::numeric_limits<float>::max()))
    {
        return false;
    }

    outCost = static_cast<float>(cost);

    if (outBreakdown != nullptr)
    {
        // 内訳は実検索と同じdouble累積値から最後にfloatへ落とします。
        // そのためDebug側で別式を使うより丸め差が小さく、Weight調整時の比較にも使えます。
        outBreakdown->PosePosition = static_cast<float>(posePositionCost);
        outBreakdown->PoseVelocity = static_cast<float>(poseVelocityCost);
        outBreakdown->TrajectoryPosition = static_cast<float>(trajectoryPositionCost);
        outBreakdown->TrajectoryDirection = static_cast<float>(trajectoryDirectionCost);
    }

    return true;
}

} // namespace Raven
