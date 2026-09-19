#pragma once

#include <cstdlib>
#include <cstring>

namespace Raven
{
// Vulkan/DX12で共通利用する起動時診断設定です。Releaseでは診断を無効にします。
// 環境変数は初期化時に参照するため、変更した場合はアプリケーションを再起動してください。
struct RHIDebugConfig
{
    bool EnableValidation = false;
    bool VerboseMessages = false;
    bool EnableGPUValidation = false;

    static RHIDebugConfig FromEnvironment()
    {
        RHIDebugConfig config{};
#if defined(_DEBUG)
        config.EnableValidation = EnvironmentEnabled("RAVEN_RHI_VALIDATION", true);
        config.VerboseMessages = EnvironmentEnabled("RAVEN_RHI_VERBOSE", false);
        config.EnableGPUValidation = EnvironmentEnabled("RAVEN_DX12_GPU_VALIDATION", false);
#endif
        return config;
    }

private:
    static bool EnvironmentEnabled(const char* name, bool defaultValue)
    {
        const char* value = std::getenv(name);
        if (value == nullptr)
        {
            return defaultValue;
        }
        return std::strcmp(value, "1") == 0 || std::strcmp(value, "true") == 0;
    }
};
} // namespace Raven
