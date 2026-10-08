#pragma once

#include "Raven/Core/Base.h"

namespace Raven
{

// Audio backendの最小Runtime境界です。SceneはServiceを所有せずApplicationから借用します。
// これによりPrimary Scene交換でBGMやMixer状態を破棄せず、Application終了時だけ停止できます。
class IAudioService
{
public:
    virtual ~IAudioService() = default;
    virtual bool Start() = 0;
    virtual void Update(float deltaTime) = 0;
    virtual void Stop() = 0;
};

class AudioService
{
public:
    ~AudioService();

    AudioService(const AudioService&) = delete;
    AudioService& operator=(const AudioService&) = delete;
    AudioService() = default;

    bool Install(Scope<IAudioService> backend);
    void Update(float deltaTime);
    void Shutdown();

    IAudioService* GetBackend() { return m_Backend.get(); }
    const IAudioService* GetBackend() const { return m_Backend.get(); }
    bool IsRunning() const { return m_Running; }

private:
    Scope<IAudioService> m_Backend;
    bool m_Running = false;
};

} // namespace Raven
