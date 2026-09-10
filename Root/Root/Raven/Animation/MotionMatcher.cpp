#include "Raven/Animation/MotionMatcher.h"

#include <cmath>
#include <utility>

namespace Raven
{

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
    m_SelectedFrameIndex = std::numeric_limits<std::size_t>::max();
    m_CurrentClipIndex = 0;
    m_CurrentTime = 0.0f;
    m_TimeSinceSwitch = 0.0f;
    m_LastSearchCost = std::numeric_limits<float>::max();
    m_HasSelection = false;
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
        std::isfinite(m_Config.MinimumSwitchInterval) == false)
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

    if (shouldSearch == true)
    {
        MotionSearchResult searchResult{};
        if (m_Database->FindBestMatch(query, m_Config.SearchWeights, searchResult) == false)
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

            // 同じClipの現在再生地点とほぼ同じ候補を検索した場合は、再選択してTimeを巻き戻さず
            // そのまま連続再生します。Databaseの1サンプル間隔を許容幅として扱います。
            const float sampleRate = m_Database->GetSampleRate();
            const float continuityTolerance =
                (sampleRate > 0.0f) ? (1.5f / sampleRate) : 0.0f;

            const bool sameClip = candidateFrame->ClipIndex == m_CurrentClipIndex;
            const bool nearCurrentTime =
                std::fabs(candidateFrame->Time - m_CurrentTime) <= continuityTolerance;

            if (sameClip == false || nearCurrentTime == false)
            {
                shouldSwitch = true;
            }
        }

        if (shouldSwitch == true)
        {
            if (SelectFrame(searchResult) == false)
            {
                return false;
            }
        }
    }

    const std::shared_ptr<AnimationClip>& clip =
        m_Database->GetClip(static_cast<std::size_t>(m_CurrentClipIndex));
    if (clip == nullptr)
    {
        return false;
    }

    return clip->Sample(skeleton, m_CurrentTime, outPose);
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

} // namespace Raven
