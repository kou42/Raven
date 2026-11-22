#include "LauncherServices.h"
#include "GitUpdate.h"
#include "ProcessRunner.h"

#include <ShlObj.h>
#include <array>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>

namespace RavenLauncher
{
    namespace fs = std::filesystem;
    using namespace std::chrono_literals;
    using winrt::Windows::Data::Json::JsonArray;
    using winrt::Windows::Data::Json::JsonObject;
    using winrt::Windows::Data::Json::JsonValue;

    namespace
    {
        std::wstring ReadUtf8(const fs::path& file)
        {
            std::ifstream stream(file, std::ios::binary);
            if (stream.is_open() == false)
            {
                throw std::runtime_error("Cannot open the input file.");
            }
            std::string data((std::istreambuf_iterator<char>(stream)), {});
            if (data.starts_with("\xef\xbb\xbf"))
            {
                data.erase(0, 3);
            }
            return winrt::to_hstring(data).c_str();
        }

        std::array<unsigned long, 4> Version(std::wstring_view version)
        {
            std::array<unsigned long, 4> values{};
            for (auto& value : values)
            {
                const auto dot = version.find(L'.');
                value = std::stoul(std::wstring(version.substr(0, dot)));
                if (dot == std::wstring_view::npos)
                {
                    break;
                }
                version.remove_prefix(dot + 1);
            }
            return values;
        }
    }

    fs::path SettingsDirectory()
    {
        PWSTR raw = nullptr;
        winrt::check_hresult(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &raw));
        const std::unique_ptr<wchar_t, decltype(&CoTaskMemFree)> owner(raw, CoTaskMemFree);
        return fs::path(raw) / L"RavenLauncher";
    }

    Configuration LoadConfiguration(const fs::path& file)
    {
        if (fs::exists(file) == false)
        {
            return {};
        }
        const auto json = JsonObject::Parse(ReadUtf8(file));
        if (json.GetNamedNumber(L"schemaVersion") != 1)
        {
            throw std::runtime_error("Unsupported configuration schema version.");
        }
        return {json.GetNamedString(L"repositoryRoot").c_str(),
            json.GetNamedString(L"solutionPath").c_str(),
            json.GetNamedString(L"configuration").c_str(), json.GetNamedString(L"platform").c_str()};
    }

    void SaveConfiguration(const fs::path& file, const Configuration& configuration)
    {
        JsonObject json;
        json.SetNamedValue(L"schemaVersion", JsonValue::CreateNumberValue(1));
        json.SetNamedValue(L"repositoryRoot", JsonValue::CreateStringValue(configuration.repositoryRoot.native()));
        json.SetNamedValue(L"solutionPath", JsonValue::CreateStringValue(configuration.solutionPath.native()));
        json.SetNamedValue(L"configuration", JsonValue::CreateStringValue(configuration.configuration));
        json.SetNamedValue(L"platform", JsonValue::CreateStringValue(configuration.platform));

        fs::create_directories(file.parent_path());
        wchar_t temporaryName[MAX_PATH];
        if (GetTempFileNameW(file.parent_path().c_str(), L"rvl", 0, temporaryName) == 0)
        {
            ThrowWindowsError("Create temporary settings file");
        }
        struct TemporaryFile
        {
            const wchar_t* path;
            ~TemporaryFile() { DeleteFileW(path); }
        } temporary{temporaryName};
        // 同じディレクトリで保存を完了してから置換し、途中失敗で前回の設定を失わないようにする。
        std::ofstream stream(temporary.path, std::ios::binary | std::ios::trunc);
        stream << winrt::to_string(json.Stringify()) << '\n';
        stream.close();
        if (stream.fail())
        {
            throw std::runtime_error("Cannot write the settings file.");
        }
        if (MoveFileExW(temporary.path, file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == FALSE)
        {
            ThrowWindowsError("Replace settings file");
        }
    }

    static fs::path ResolveSolution(const Configuration& configuration)
    {
        if (configuration.repositoryRoot.is_absolute() == false)
        {
            throw std::runtime_error("Select an absolute repository path.");
        }
        if (configuration.solutionPath.has_root_path())
        {
            throw std::runtime_error("The solution path must be relative to the repository.");
        }
        const auto root = fs::weakly_canonical(configuration.repositoryRoot);
        const auto solution = fs::weakly_canonical(root / configuration.solutionPath);
        const auto relative = solution.lexically_relative(root);
        if (relative.empty() || *relative.begin() == L".." ||
            _wcsicmp(solution.extension().c_str(), L".sln") != 0 || fs::is_regular_file(solution) == false)
        {
            throw std::runtime_error("The solution must be an existing .sln file inside the repository.");
        }

        return solution;
    }

    static std::vector<std::wstring> ReadSolutionConfigurations(const fs::path& solution)
    {
        const auto trim = [](std::wstring_view value)
        {
            const auto start = value.find_first_not_of(L" \t\r");
            return start == std::wstring_view::npos ? std::wstring_view{} :
                value.substr(start, value.find_last_not_of(L" \t\r") - start + 1);
        };
        std::vector<std::wstring> configurations;
        std::wistringstream stream(ReadUtf8(solution));
        bool inSection = false;
        for (std::wstring line; std::getline(stream, line);)
        {
            const auto entry = trim(line);
            if (inSection == false)
            {
                inSection = entry.starts_with(L"GlobalSection(SolutionConfigurationPlatforms)");
                continue;
            }
            if (entry == L"EndGlobalSection")
            {
                if (configurations.empty() == false)
                {
                    return configurations;
                }
                break;
            }
            const auto equal = entry.find(L'=');
            if (equal == std::wstring_view::npos)
            {
                continue;
            }
            const auto pair = trim(entry.substr(0, equal));
            const auto delimiter = pair.find(L'|');
            if (delimiter == std::wstring_view::npos || trim(pair.substr(0, delimiter)).empty() ||
                trim(pair.substr(delimiter + 1)).empty() || pair.find(L'|', delimiter + 1) != std::wstring_view::npos)
            {
                continue;
            }
            if (std::ranges::find(configurations, pair) == configurations.end())
            {
                configurations.emplace_back(pair);
            }
        }
        throw std::runtime_error("The solution does not define any build configurations.");
    }

    std::vector<std::wstring> SolutionConfigurations(const Configuration& configuration)
    {
        return ReadSolutionConfigurations(ResolveSolution(configuration));
    }

    fs::path ValidateConfiguration(Configuration& configuration, const std::stop_token stop)
    {
        configuration.repositoryRoot = FindGitRoot(FindGit(), configuration.repositoryRoot, stop);
        const auto solution = ResolveSolution(configuration);
        const auto choices = ReadSolutionConfigurations(solution);
        if (std::ranges::find(choices, configuration.configuration + L"|" + configuration.platform) == choices.end())
        {
            throw std::runtime_error("The solution does not define the selected configuration and platform.");
        }
        return solution;
    }

    static fs::path FindVisualStudioTool(const fs::path& directory, std::stop_token stop, bool requireIde)
    {
        PWSTR raw = nullptr;
        winrt::check_hresult(SHGetKnownFolderPath(FOLDERID_ProgramFilesX86, KF_FLAG_DEFAULT, nullptr, &raw));
        const std::unique_ptr<wchar_t, decltype(&CoTaskMemFree)> owner(raw, CoTaskMemFree);
        const auto vswhere = fs::path(raw) / L"Microsoft Visual Studio/Installer/vswhere.exe";
        if (fs::is_regular_file(vswhere) == false)
        {
            throw std::runtime_error("Visual Studio Installer (vswhere.exe) was not found.");
        }
        std::vector<std::wstring> arguments{L"-products", L"*", L"-version", L"[18.0,19.0)", L"-requires",
            L"Microsoft.Component.MSBuild"};
        if (requireIde)
        {
            arguments.emplace_back(L"Microsoft.VisualStudio.Component.VC.Tools.x86.x64");
        }
        arguments.insert(arguments.end(), {L"-format", L"json", L"-utf8"});
        const auto result = RunProcess(vswhere, arguments, directory, stop, 15s);
        if (result.exitCode != 0)
        {
            throw std::runtime_error("vswhere failed with exit code " + std::to_string(result.exitCode) + ".");
        }
        auto candidates = JsonArray::Parse(winrt::to_hstring(result.output));
        fs::path best;
        std::array<unsigned long, 4> bestVersion{};
        for (const auto& value : candidates)
        {
            const auto candidate = value.GetObject();
            const auto path = fs::path(candidate.GetNamedString(L"installationPath").c_str());
            const auto ide = path / L"Common7/IDE/devenv.exe";
            const auto msbuild = path / L"MSBuild/Current/Bin/amd64/MSBuild.exe";
            const auto version = Version(candidate.GetNamedString(L"installationVersion"));
            if ((requireIde == false || fs::is_regular_file(ide)) &&
                fs::is_regular_file(msbuild) && version > bestVersion)
            {
                best = requireIde ? ide : msbuild;
                bestVersion = version;
            }
        }
        if (best.empty())
        {
            throw std::runtime_error(requireIde ? "No Visual Studio 2026 IDE with MSBuild and C++ tools was found." :
                "MSBuild was not found. Install the MSBuild component using Visual Studio 2026 Installer.");
        }
        return best;
    }

    fs::path FindVisualStudio(const fs::path& directory, std::stop_token stop)
    {
        return FindVisualStudioTool(directory, stop, true);
    }

    fs::path FindMSBuild(const fs::path& directory, std::stop_token stop)
    {
        return FindVisualStudioTool(directory, stop, false);
    }

    EnvironmentReport DiagnoseEnvironment(Configuration configuration, std::stop_token stop)
    {
        EnvironmentReport report{L"Raven Launcher environment"};
        const auto check = [&](std::wstring_view name, const auto& work)
        {
            if (stop.stop_requested())
            {
                throw std::runtime_error("Operation cancelled.");
            }
            report.details += L"\r\n";
            report.details += name;
            try
            {
                report.details += L": " + work();
            }
            catch (const winrt::hresult_error& error)
            {
                report.passed = false;
                report.details += L": " + std::wstring(error.message());
            }
            catch (const std::system_error& error)
            {
                report.passed = false;
                report.details += L": Windows error " + std::to_wstring(error.code().value());
            }
            catch (const std::exception& error)
            {
                report.passed = false;
                report.details += L": " + std::wstring(winrt::to_hstring(error.what()));
            }
        };

        fs::path solution;
        check(L"Git worktree and solution", [&]
        {
            solution = ValidateConfiguration(configuration, stop);
            return solution.filename().native() + L" (" + configuration.configuration + L"|" +
                configuration.platform + L")";
        });
        const auto directory = solution.empty() ? SettingsDirectory().parent_path() : configuration.repositoryRoot;
        check(L"Git", [&]
        {
            auto result = RunProcess(FindGit(), {L"--version"}, directory, stop, 10s);
            if (result.exitCode != 0 || result.output.empty())
            {
                throw std::runtime_error("Git did not report its version.");
            }
            result.output.erase(result.output.find_last_not_of("\r\n") + 1);
            return std::wstring(winrt::to_hstring(result.output));
        });
        check(L"Visual Studio 2026", [&]
        {
            FindVisualStudio(directory, stop);
            return std::wstring(L"IDE detected");
        });
        fs::path msbuild;
        check(L"MSBuild", [&]
        {
            msbuild = FindMSBuild(directory, stop);
            return std::wstring(L"x64 MSBuild detected");
        });

        const auto project = configuration.repositoryRoot / L"Root/Root/Root.vcxproj";
        if (solution.empty() == false && msbuild.empty() == false &&
            fs::is_regular_file(project) && configuration.solutionPath.lexically_normal() == fs::path(L"Root/Root.sln"))
        {
            check(L"Raven MSVC and Windows SDK", [&]
            {
                if (configuration.configuration.find_first_of(L";,%\"") != std::wstring::npos ||
                    (configuration.platform != L"x86" && configuration.platform != L"x64"))
                {
                    throw std::runtime_error("The selected configuration cannot be evaluated for the Raven project.");
                }
                const bool x86 = configuration.platform == L"x86";
                const auto result = RunProcess(msbuild, {project.native(), L"-nologo",
                    L"-p:Configuration=" + configuration.configuration, x86 ? L"-p:Platform=Win32" : L"-p:Platform=x64",
                    L"-p:PreferredToolArchitecture=x64",
                    L"-getProperty:PlatformToolset,VCToolsInstallDir,WindowsSdkDir,TargetPlatformVersion"},
                    directory, stop, 30s);
                if (result.exitCode != 0)
                {
                    throw std::runtime_error("MSBuild could not evaluate the C++ project. Check the C++ workload.");
                }
                const auto properties = JsonObject::Parse(winrt::to_hstring(result.output)).GetNamedObject(L"Properties");
                const std::wstring toolset(properties.GetNamedString(L"PlatformToolset"));
                const fs::path tools(properties.GetNamedString(L"VCToolsInstallDir").c_str());
                const fs::path sdk(properties.GetNamedString(L"WindowsSdkDir").c_str());
                const std::wstring version(properties.GetNamedString(L"TargetPlatformVersion"));
                const auto arch = x86 ? L"x86" : L"x64";
                const bool compilerFound = tools.empty() == false &&
                    fs::is_regular_file(tools / L"bin/Hostx64" / arch / L"cl.exe");
                const bool sdkFound = sdk.empty() == false && version.empty() == false &&
                    fs::is_regular_file(sdk / L"Include" / version / L"um/Windows.h") &&
                    fs::is_regular_file(sdk / L"Lib" / version / L"um" / arch / L"kernel32.lib");
                report.passed = report.passed && compilerFound && sdkFound;
                return toolset + (compilerFound ? L" detected" : L" missing; install the matching MSVC component") +
                    L"; Windows SDK " + version + (sdkFound ? L" detected" : L" missing; install the matching SDK");
            });
        }
        if (stop.stop_requested())
        {
            throw std::runtime_error("Operation cancelled.");
        }
        return report;
    }
}
