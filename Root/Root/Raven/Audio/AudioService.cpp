#include "Raven/Audio/AudioService.h"

#include <algorithm>

namespace Raven
{

AudioService::~AudioService()
{
    Shutdown();
}

bool AudioService::Install(Scope<IAudioService> backend)
{
    Shutdown();
    if (backend == nullptr)
    {
        return false;
    }

    // Start失敗時もStop可能な部分初期化状態をbackend自身に整理させます。
    if (backend->Start() == false)
    {
        backend->Stop();
        return false;
    }

    m_Backend = std::move(backend);
    m_Running = true;
    return true;
}

void AudioService::Update(float deltaTime)
{
    if (m_Running == true && m_Backend != nullptr)
    {
        m_Backend->Update(std::max(deltaTime, 0.0f));
    }
}

void AudioService::Shutdown()
{
    if (m_Backend != nullptr)
    {
        if (m_Running == true)
        {
            m_Backend->Stop();
        }
        m_Backend.reset();
    }
    m_Running = false;
}

} // namespace Raven
