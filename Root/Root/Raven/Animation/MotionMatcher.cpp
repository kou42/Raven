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
    m_LastOutputPose = SkeletonPose{};

    m_SelectedFrameIndex = std::numeric_limits<std::size_t>::max();
    m_CurrentClipIndex = 0;
    m_CurrentTime = 0.0f;
    m_TimeSinceSwitch = 0.0f;
    m_LastSearchCost = std::numeric_limits<float>::max();
    m_HasSelection = false;
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

    // 初回選択、または最低保持時間を過ぎたときだけDatabase検索を行います。
    // MinimumSwitchInterval中に毎Frame検索しても結果を採用できずCostだけが揺れるため、
    // 検索負荷とDebug値の意味を揃えるために検索自体を抑制します。
    const bool shouldSearch =
        m_HasSelection == false ||
        m_TimeSinceSwitch >= m_Config.MinimumSwitchInterval;

    bool switchedThisFrame = false;

    if (shouldSearch == true)
    {
        MotionSearchResult searchResult{};
        if (m_Database->FindBestMatch(query, m_Config.SearchWeights, searchResult) == false)
        {
            return false;
        }

        // Debug表示ではFeatureそのものの品質を確認できるよう、Bias適用前の最良Costを保持します。
        m_LastSearchCost = searchResult.Cost;

        bool shouldSwitch = m_HasSelection == false;

        if (m_HasSelection == true)
        {
            const MotionFrame* candidateFrame = m_Database->GetFrame(searchResult.FrameIndex);
            if (candidateFrame == nullptr)
            {
                return false;
            }

            // 同じClipの現在再生地点とほぼ同じ候補を検索した場合は、再選択してTimeを巻き戻さず
            // そのまま連続再生します。Databaseの1サンプル間隔を許容幅として扱います。
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

                // Loop境界ではDuration直前と0秒が隣接するため、線形時間差ではなく円環距離を使います。
                candidateTimeDistance = std::min(
                    candidateTimeDistance,
                    currentClip->GetDuration() - candidateTimeDistance);
            }

            const bool nearCurrentTime = candidateTimeDistance <= continuityTolerance;

            if (sameClip == false || nearCurrentTime == false)
            {
                shouldSwitch = true;

                // ====================================================================
                // Stay Bonus / Switch Cost
                // ====================================================================
                // Global最良候補だけを見ると、ほぼ同CostのFrame同士で毎検索時にJumpしやすくなります。
                // 現在の連続再生地点に最も近いDatabase Frameも同じQueryで評価し、
                //   continuationCost - StayBonus <= candidateCost + SwitchCost
                // なら現在Motionを維持します。
                //
                // MinimumSwitchIntervalは「切替直後の時間的Lock」、このBiasは「Lock解除後のCostヒステリシス」
                // と役割を分けることで、入力が明確に変化した場合は新候補へ切り替えつつ微小なCost揺れを抑えます。
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
            // 新しいClipへはこのFrameで即座に切り替えます。
            // ただし表示Poseは直前Frameとの差分をOffsetとして保持してから減衰させるため、
            // CrossFadeのように旧Clipを継続SampleせずPoseの連続性だけを維持できます。
            if (m_Inertializer.Begin(skeleton, m_LastOutputPose, targetPose) == false)
            {
                return false;
            }
        }
        else if (switchedThisFrame == true)
        {
            // 初回選択では比較元Poseがないため、Inertializationを開始しません。
            m_Inertializer.Reset();
        }

        // 切替Frameではelapsed=0のOffsetをそのまま適用し、直前表示Poseを再現します。
        // 次Frame以降にdeltaTime分ずつ減衰させることで、切替瞬間に1Frame分先へ進んだOffsetを
        // 適用して小さなPose Jumpを生むことを避けます。
        const float inertialDeltaTime = switchedThisFrame ? 0.0f : deltaTime;
        if (m_Inertializer.Apply(skeleton, targetPose, inertialDeltaTime, outPose) == false)
        {
            return false;
        }
    }
    else
    {
        // Debugで検索先Poseそのものを確認できるよう、Inertialization無効時は完全に迂回します。
        m_Inertializer.Reset();
        outPose = targetPose;
    }

    m_LastOutputPose = outPose;
    m_HasLastOutputPose = true;
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
        // AnimationClipへLoop状態を持たせず、再生InstanceであるMotionMatcherだけがWrapします。
        // fmod後が負になる経路はdeltaTime>=0の契約上ありません。
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

        // Loop再生では0秒とDuration直前は時間軸上で隣接しています。
        // Wrap境界だけ別Motion扱いになるのを防ぐため、円環上の短い方の距離を使用します。
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
    float& outCost) const
{
    outCost = std::numeric_limits<float>::max();

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

    double cost = 0.0;

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

        cost += static_cast<double>(weights.PosePosition) * LengthSquared(positionDelta);
        cost += static_cast<double>(weights.PoseVelocity) * LengthSquared(velocityDelta);
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

        cost += static_cast<double>(weights.TrajectoryPosition) * LengthSquared(positionDelta);
        cost += static_cast<double>(weights.TrajectoryDirection) * LengthSquared(directionDelta);
    }

    if (std::isfinite(cost) == false ||
        cost > static_cast<double>(std::numeric_limits<float>::max()))
    {
        return false;
    }

    outCost = static_cast<float>(cost);
    return true;
}

} // namespace Raven
