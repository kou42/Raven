#pragma once

#include <filesystem>
#include <stop_token>
#include <string>

namespace RavenLauncher
{
    struct GitUpdatePlan
    {
        std::filesystem::path root;
        std::wstring branch;
        std::wstring upstream;
        std::wstring remote;
        std::wstring remoteRef;
        std::wstring before;
        std::wstring target;

        bool operator==(const GitUpdatePlan&) const = default;
    };

    std::filesystem::path FindGit();
    std::filesystem::path FindGitRoot(const std::filesystem::path& git,
        const std::filesystem::path& directory, std::stop_token stop);
    GitUpdatePlan CheckGitUpdate(const std::filesystem::path& directory, std::stop_token stop);
    std::wstring ApplyGitUpdate(const GitUpdatePlan& plan);
}
