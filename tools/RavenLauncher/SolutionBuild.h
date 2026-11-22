#pragma once

#include "LauncherServices.h"

#include <functional>

namespace RavenLauncher
{
    struct BuildResult
    {
        std::wstring status;
        std::wstring details;
        bool succeeded = false;
    };

    BuildResult BuildSolution(Configuration settings, const std::filesystem::path& settingsDirectory,
        std::stop_token stop, const std::function<void(std::wstring)>& report);
}
