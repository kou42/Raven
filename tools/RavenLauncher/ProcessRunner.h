#pragma once

#include <Windows.h>
#include <chrono>
#include <filesystem>
#include <functional>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

namespace RavenLauncher
{
    struct ProcessResult
    {
        DWORD exitCode = 0;
        std::string output;
        std::string errors;
    };

    using ProcessOutput = std::function<void(std::string_view bytes, bool standardError)>;

    ProcessResult RunProcess(const std::filesystem::path& executable,
        const std::vector<std::wstring>& arguments, const std::filesystem::path& directory,
        std::stop_token stop, std::chrono::milliseconds timeout, const ProcessOutput& output = {});
    void LaunchProcess(const std::filesystem::path& executable,
        const std::vector<std::wstring>& arguments, const std::filesystem::path& directory);
    [[noreturn]] void ThrowWindowsError(const char* operation);
}
