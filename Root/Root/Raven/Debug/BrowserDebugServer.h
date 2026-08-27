#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>

namespace Raven
{

// ============================================================================
// BrowserDebugFilterState
// ============================================================================
// Browser側で選択したReject FunnelのParticle / Triangle Filterです。
// HasParticle / HasTriangleを分けて持つことで、0番Indexと「未指定」を混同しません。
struct BrowserDebugFilterState
{
    bool HasParticle = false;
    uint32_t ParticleIndex = 0u;

    bool HasTriangle = false;
    uint32_t TriangleIndex = 0u;
};

// ============================================================================
// BrowserDebugServer
// ============================================================================
// Debug Browser Viewerをfile://ではなくlocalhost HTTPで配信し、BrowserからRaven Processへ
// Filter選択を返すための最小HTTP Bridgeです。
//
// セキュリティ上、listen先は127.0.0.1に固定します。
// 外部NICへbindしないため、同一PC外からこのDebug endpointへ接続する用途は持ちません。
//
// 対応するHTTP機能は意図的に最小限です。
//   GET /Viewer.html
//   GET /Startup.svg
//   GET /CandidateRejects.svg
//   GET /filter?particle=<index>&triangle=<index>
//
// Web Frameworkを導入せず、Debug用途だけで完結させることでRuntime依存を増やしません。
class BrowserDebugServer
{
public:
    static BrowserDebugServer& Get();

    BrowserDebugServer(const BrowserDebugServer&) = delete;
    BrowserDebugServer& operator=(const BrowserDebugServer&) = delete;

    ~BrowserDebugServer();

    bool Start(const std::filesystem::path& rootDirectory, uint16_t port = 18765u);
    void Stop();

    bool IsRunning() const { return m_Running.load(); }
    uint16_t GetPort() const { return m_Port; }
    std::string GetViewerUrl() const;

    BrowserDebugFilterState GetFilterState() const;

private:
    BrowserDebugServer() = default;

    void ServerThreadMain();
    void HandleClient(uintptr_t clientSocketValue);

    void SetFilterState(const BrowserDebugFilterState& state);

private:
    std::filesystem::path m_RootDirectory;
    uint16_t m_Port = 0u;

    std::atomic<bool> m_Running{ false };
    std::thread m_ServerThread;

    // SOCKETはwinsock2.hをHeaderへ漏らさないためuintptr_tとして保持します。
    // 実際の変換はWindows専用.cpp内だけで行います。
    uintptr_t m_ListenSocketValue = 0u;

    mutable std::mutex m_FilterMutex;
    BrowserDebugFilterState m_FilterState{};
};

} // namespace Raven
