#include "Raven/Animation/MotionDatabase.h"

#include <cmath>
#include <limits>
#include <utility>

namespace Raven
{

bool MotionDatabase::AddClip(std::shared_ptr<AnimationClip> clip)
{
    if (clip == nullptr)
    {
        return false;
    }

    // 同じClipを複数回登録すると完全に同一の検索候補が増え、将来のCost比較で
    // 不要な同点候補を作るため、shared_ptrが指す実体単位で重複を拒否します。
    for (const std::shared_ptr<AnimationClip>& existingClip : m_Clips)
    {
        if (existingClip.get() == clip.get())
        {
            return false;
        }
    }

    m_Clips.emplace_back(std::move(clip));

    // Clip構成が変わった時点で既存Frame列は対応関係が古くなります。
    // Build()し直すまで未構築状態として扱い、古い検索候補を参照させません。
    m_Frames.clear();
    m_SampleRate = 0.0f;
    return true;
}

void MotionDatabase::Clear()
{
    m_Clips.clear();
    m_Frames.clear();
    m_SampleRate = 0.0f;
}

bool MotionDatabase::Build(float sampleRate)
{
    m_Frames.clear();
    m_SampleRate = 0.0f;

    if (sampleRate <= 0.0f || std::isfinite(sampleRate) == false)
    {
        return false;
    }

    if (m_Clips.empty() == true)
    {
        return false;
    }

    if (m_Clips.size() > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()))
    {
        return false;
    }

    std::size_t totalFrameCount = 0;

    // 先に全Clipを検証して必要Frame数を求めます。
    // 検証途中で失敗した場合に一部Clipだけを持つDatabaseを残さないため、
    // 実際のMotionFrame生成は全Clipの検証後に行います。
    for (const std::shared_ptr<AnimationClip>& clip : m_Clips)
    {
        if (clip == nullptr)
        {
            return false;
        }

        const float duration = clip->GetDuration();
        if (duration <= 0.0f || std::isfinite(duration) == false)
        {
            return false;
        }

        const double exactFrameCount =
            std::ceil(static_cast<double>(duration) * static_cast<double>(sampleRate));

        if (exactFrameCount <= 0.0 ||
            exactFrameCount > static_cast<double>(std::numeric_limits<std::size_t>::max()))
        {
            return false;
        }

        const std::size_t clipFrameCount = static_cast<std::size_t>(exactFrameCount);
        if (clipFrameCount > (std::numeric_limits<std::size_t>::max() - totalFrameCount))
        {
            return false;
        }

        totalFrameCount += clipFrameCount;
    }

    m_Frames.reserve(totalFrameCount);

    const double sampleInterval = 1.0 / static_cast<double>(sampleRate);

    for (std::size_t clipIndex = 0; clipIndex < m_Clips.size(); ++clipIndex)
    {
        const float duration = m_Clips[clipIndex]->GetDuration();
        const std::size_t clipFrameCount = static_cast<std::size_t>(
            std::ceil(static_cast<double>(duration) * static_cast<double>(sampleRate)));

        for (std::size_t frameIndex = 0; frameIndex < clipFrameCount; ++frameIndex)
        {
            MotionFrame frame{};
            frame.ClipIndex = static_cast<std::uint32_t>(clipIndex);
            frame.Time = static_cast<float>(static_cast<double>(frameIndex) * sampleInterval);

            // ceil()で個数を決めているため理論上Duration未満ですが、floatへ戻す際の丸めで
            // 終端以上になった場合は候補を追加しません。終端重複を作らないというBuild契約を
            // 浮動小数点誤差があっても維持します。
            if (frame.Time < duration)
            {
                m_Frames.emplace_back(frame);
            }
        }
    }

    if (m_Frames.empty() == true)
    {
        return false;
    }

    m_SampleRate = sampleRate;
    return true;
}

const std::shared_ptr<AnimationClip>& MotionDatabase::GetClip(std::size_t clipIndex) const
{
    static const std::shared_ptr<AnimationClip> NullClip{};

    if (clipIndex >= m_Clips.size())
    {
        return NullClip;
    }

    return m_Clips[clipIndex];
}

const MotionFrame* MotionDatabase::GetFrame(std::size_t frameIndex) const
{
    if (frameIndex >= m_Frames.size())
    {
        return nullptr;
    }

    return &m_Frames[frameIndex];
}

} // namespace Raven
