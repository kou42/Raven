#include "Raven/Animation/Animator.h"

#include <algorithm>
#include <cmath>

namespace Raven
{

void Animator::Play(std::shared_ptr<AnimationClip> clip, bool restart)
{
    if (!clip)
    {
        Stop();
        m_Clip.reset();
        return;
    }

    // 同じClipをrestart=falseでPlayした場合は現在位置を維持します。
    // Clipが切り替わった場合は、別Animationの時刻を引き継ぐ意味がないため
    // restart指定に関係なく先頭から開始します。
    const bool clipChanged = (m_Clip != clip);
    m_Clip = std::move(clip);

    if (restart || clipChanged)
    {
        m_CurrentTime = 0.0f;
    }

    m_Playing = true;
    m_Paused = false;
    EvaluateCurrentPose();
}

void Animator::Pause()
{
    if (!m_Playing)
    {
        return;
    }

    m_Paused = true;
}

void Animator::Resume()
{
    if (!m_Playing || !m_Clip)
    {
        return;
    }

    m_Paused = false;
}

void Animator::Stop()
{
    // StopではClip参照を残します。
    // これによりStop後も先頭Poseを保持でき、再度Play(m_Clip)する場合にも
    // Assetを外側で取り直す必要がありません。
    m_CurrentTime = 0.0f;
    m_Playing = false;
    m_Paused = false;
    EvaluateCurrentPose();
}

void Animator::Update(float dt)
{
    if (!m_Playing || m_Paused || !m_Clip)
    {
        return;
    }

    const float duration = m_Clip->GetDuration();

    // Duration=0のClipは時間を進められません。
    // Sample(0)だけ評価して停止扱いにすることでfmodの0除算も防ぎます。
    if (duration <= 0.0f)
    {
        m_CurrentTime = 0.0f;
        m_CurrentPose = m_Clip->Sample(0.0f);
        m_Playing = false;
        return;
    }

    m_CurrentTime += dt * m_Speed;

    if (m_Loop)
    {
        // fmodは負の入力に対して負値を返すため、逆再生(speed < 0)でも
        // [0, duration)へ収まるように補正します。
        m_CurrentTime = std::fmod(m_CurrentTime, duration);
        if (m_CurrentTime < 0.0f)
        {
            m_CurrentTime += duration;
        }
    }
    else
    {
        // 非Loop再生ではClip端へ到達した時点で停止します。
        // Speedを負にすれば逆方向へも進められるため両端を判定します。
        if (m_CurrentTime >= duration)
        {
            m_CurrentTime = duration;
            m_Playing = false;
        }
        else if (m_CurrentTime <= 0.0f)
        {
            m_CurrentTime = 0.0f;

            // 正方向再生をPlay直後にdt=0で停止させないため、逆再生時だけ停止します。
            if (m_Speed < 0.0f)
            {
                m_Playing = false;
            }
        }
    }

    EvaluateCurrentPose();
}

void Animator::EvaluateCurrentPose()
{
    if (!m_Clip)
    {
        m_CurrentPose = TransformPose{};
        return;
    }

    const float duration = m_Clip->GetDuration();
    m_CurrentTime = std::clamp(m_CurrentTime, 0.0f, duration);
    m_CurrentPose = m_Clip->Sample(m_CurrentTime);
}

} // namespace Raven
