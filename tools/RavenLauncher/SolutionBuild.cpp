#include "SolutionBuild.h"
#include "ProcessRunner.h"

#include <ShlObj.h>
#include <array>
#include <fstream>
#include <winrt/base.h>

namespace RavenLauncher
{
    namespace fs = std::filesystem;
    using namespace std::chrono_literals;

    namespace
    {
        std::wstring Decode(std::string_view bytes)
        {
            if (bytes.empty())
            {
                return {};
            }
            UINT page = CP_UTF8;
            DWORD flags = MB_ERR_INVALID_CHARS;
            int size = MultiByteToWideChar(page, flags, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
            if (size == 0)
            {
                page = CP_OEMCP;
                flags = 0;
                size = MultiByteToWideChar(page, flags, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
            }
            std::wstring text(size, L'\0');
            MultiByteToWideChar(page, flags, bytes.data(), static_cast<int>(bytes.size()), text.data(), size);
            return text;
        }

        void HidePath(std::wstring& line, const fs::path& path, std::wstring_view label)
        {
            if (path.empty())
            {
                return;
            }
            for (const auto& form : {path.native(), path.generic_wstring()})
            {
                const auto length = static_cast<int>(form.size());
                for (size_t position = 0; position + form.size() <= line.size();)
                {
                    if (CompareStringOrdinal(line.data() + position, length, form.data(), length, TRUE) == CSTR_EQUAL)
                    {
                        line.replace(position, form.size(), label);
                        position += label.size();
                    }
                    else
                    {
                        ++position;
                    }
                }
            }
        }

        std::wstring NewLogName()
        {
            GUID id{};
            winrt::check_hresult(CoCreateGuid(&id));
            wchar_t buffer[40];
            if (StringFromGUID2(id, buffer, static_cast<int>(std::size(buffer))) == 0)
            {
                throw std::runtime_error("Could not generate a build log name.");
            }
            return std::wstring(L"build-") + buffer + L".log";
        }

        struct BuildLog
        {
            std::ofstream file;
            const fs::path& root;
            fs::path profile;
            const std::function<void(std::wstring)>& report;
            std::array<std::string, 2> pending;
            size_t warnings = 0;
            bool toolsetMissing = false;
            bool sdkMissing = false;
            bool cppTargetsMissing = false;
            bool fileFailed = false;

            void Line(std::wstring text, bool standardError)
            {
                if (text.ends_with(L'\r'))
                {
                    text.pop_back();
                }
                if (text.empty())
                {
                    return;
                }
                // MSBuild の診断に含まれる絶対パスを揃え、ユーザープロファイル名を記録しない。
                HidePath(text, root, L"<repo>");
                HidePath(text, profile, L"<profile>");
                if (text.contains(L": warning ") || text.contains(L": 警告 "))
                {
                    ++warnings;
                }
                toolsetMissing = toolsetMissing || text.contains(L"MSB8020");
                sdkMissing = sdkMissing || text.contains(L"MSB8036");
                cppTargetsMissing = cppTargetsMissing || (text.contains(L"MSB4019") && text.contains(L"Microsoft.Cpp."));
                if (standardError)
                {
                    text.insert(0, L"[stderr] ");
                }
                file << winrt::to_string(text) << "\r\n";
                fileFailed = fileFailed || file.fail();
                report(std::move(text));
            }

            void Receive(std::string_view bytes, bool standardError)
            {
                auto& remainder = pending[standardError ? 1 : 0];
                remainder.append(bytes);
                size_t start = 0;
                for (auto end = remainder.find('\n'); end != std::string::npos; end = remainder.find('\n', start))
                {
                    Line(Decode(std::string_view(remainder).substr(start, end - start)), standardError);
                    start = end + 1;
                }
                remainder.erase(0, start);
                if (start != 0)
                {
                    file.flush();
                    fileFailed = fileFailed || file.fail();
                }
            }

            void Finish()
            {
                Line(Decode(pending[0]), false);
                Line(Decode(pending[1]), true);
                file.flush();
                fileFailed = fileFailed || file.fail();
            }
        };

        std::wstring Property(std::wstring_view name, const std::wstring& value)
        {
            // MSBuild のプロパティ構文で別プロパティと解釈される区切り文字を拒否する。
            if (value.find_first_of(L";,%\"") != std::wstring::npos)
            {
                throw std::runtime_error("The configuration or platform contains an unsupported MSBuild separator.");
            }
            return L"-p:" + std::wstring(name) + L"=" + value;
        }
    }

    BuildResult BuildSolution(Configuration settings, const fs::path& settingsDirectory,
        std::stop_token stop, const std::function<void(std::wstring)>& report)
    {
        const auto solution = ValidateConfiguration(settings, stop);
        const auto msbuild = FindMSBuild(settings.repositoryRoot, stop);
        const auto configuration = Property(L"Configuration", settings.configuration);
        const auto platform = Property(L"Platform", settings.platform);
        SaveConfiguration(settingsDirectory / L"config.json", settings);
        const auto logDirectory = settingsDirectory / L"build-logs";
        fs::create_directories(logDirectory);
        const auto logName = NewLogName();

        PWSTR rawProfile = nullptr;
        fs::path profile;
        const HRESULT profileResult = SHGetKnownFolderPath(FOLDERID_Profile, KF_FLAG_DEFAULT, nullptr, &rawProfile);
        const std::unique_ptr<wchar_t, decltype(&CoTaskMemFree)> profileOwner(rawProfile, CoTaskMemFree);
        if (SUCCEEDED(profileResult) && rawProfile != nullptr)
        {
            profile = rawProfile;
        }
        BuildLog log{std::ofstream(logDirectory / logName, std::ios::binary), settings.repositoryRoot,
            std::move(profile), report, {}};
        if (log.file.is_open() == false)
        {
            throw std::runtime_error("Could not create the build log.");
        }

        const auto logPath = L"%LOCALAPPDATA%\\RavenLauncher\\build-logs\\" + logName;
        log.Line(L"MSBuild " + solution.filename().native() + L" (" +
            settings.configuration + L"|" + settings.platform + L")", false);
        BuildResult result;
        try
        {
            // x86 を選んだ場合も x64 ホストのツールを使い、生成するバイナリだけを選択構成に合わせる。
            const auto process = RunProcess(msbuild, {solution.native(), L"-t:Build", L"-nologo",
                L"-m", L"-nr:false", L"-verbosity:minimal", L"-clp:NoSummary",
                L"-p:PreferredToolArchitecture=x64", configuration, platform},
                settings.repositoryRoot, stop, 60min, [&log](std::string_view bytes, bool error)
                {
                    log.Receive(bytes, error);
                });
            result.status = process.exitCode == 0 ? L"Build succeeded" : L"Build failed";
            result.succeeded = process.exitCode == 0;
            result.details = result.status + L". MSBuild exit code " + std::to_wstring(process.exitCode) + L".";
        }
        catch (const std::system_error& error)
        {
            result = {L"Build failed", L"Could not run MSBuild. Windows error " + std::to_wstring(error.code().value()) + L"."};
        }
        catch (const std::runtime_error& error)
        {
            result.status = stop.stop_requested() ? L"Build cancelled" : L"Build failed";
            result.details = result.status + L". " + Decode(error.what());
        }

        log.Finish();
        result.details += L" Warnings: " + std::to_wstring(log.warnings) + L".";
        if (log.toolsetMissing || log.cppTargetsMissing)
        {
            result.details += L"\r\nInstall the requested MSVC toolset and C++ build tools via Visual Studio Installer.";
        }
        if (log.sdkMissing)
        {
            result.details += L"\r\nInstall the Windows SDK version listed in the MSBuild diagnostics.";
        }
        result.details += L"\r\nBuild log: " + logPath;
        log.file << winrt::to_string(result.details) << "\r\n";
        log.file.close();
        if (log.fileFailed || log.file.fail())
        {
            result.details += L"\r\nThe build log could not be saved completely.";
        }
        return result;
    }
}
