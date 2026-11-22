#pragma once

#include <filesystem>
#include <stop_token>
#include <string>
#include <vector>

namespace RavenLauncher
{
    struct Configuration
    {
        std::filesystem::path repositoryRoot;
        std::filesystem::path solutionPath = L"Root/Root.sln";
        std::wstring configuration = L"Debug";
        std::wstring platform = L"x64";
    };

    struct EnvironmentReport
    {
        std::wstring details;
        bool passed = true;
    };

    std::filesystem::path SettingsDirectory();
    Configuration LoadConfiguration(const std::filesystem::path& file);
    void SaveConfiguration(const std::filesystem::path& file, const Configuration& configuration);
    std::vector<std::wstring> SolutionConfigurations(const Configuration& configuration);
    std::filesystem::path ValidateConfiguration(Configuration& configuration, std::stop_token stop);
    std::filesystem::path FindVisualStudio(const std::filesystem::path& directory, std::stop_token stop);
    std::filesystem::path FindMSBuild(const std::filesystem::path& directory, std::stop_token stop);
    EnvironmentReport DiagnoseEnvironment(Configuration configuration, std::stop_token stop);
}
