#include "LauncherServices.h"
#include "GitUpdate.h"
#include "ProjectSync.h"
#include "ProcessRunner.h"
#include "SolutionBuild.h"

#include <ShlObj.h>
#include <fstream>
#include <deque>
#include <functional>
#include <future>
#include <iterator>
#include <mutex>
#include <optional>
#include <utility>
#include <winrt/base.h>

namespace
{
    using namespace RavenLauncher;
    namespace fs = std::filesystem;
    using namespace std::chrono_literals;

    enum Control { Browse = 101, OpenIde, Cancel, BuildChoice, SolutionPath, RepositoryPath,
        LogView, UpdateGit, SyncProjects, Build, RefreshConfigs, UpdateAndOpen, Setup, Diagnose };
    enum class Step { GitCheck, GitApply, SyncCheck, SyncApply, Build, Open };
    constexpr UINT_PTR PollJob = 1;

    struct Outcome
    {
        Configuration settings;
        std::wstring message;
        bool opened = false;
        std::optional<GitUpdatePlan> gitPlan{};
        std::optional<ProjectSyncPlan> syncPlan{};
        std::wstring status{};
        bool succeeded = true;
        bool settingsSaved = false;
        bool showDiagnostics = false;
    };

    std::wstring ErrorMessage()
    {
        try
        {
            throw;
        }
        catch (const winrt::hresult_error& error)
        {
            return error.message().c_str();
        }
        catch (const std::exception& error)
        {
            // system_error のメッセージは Windows のローカルコードページの場合がある。
            const std::string_view message(error.what());
            const int size = MultiByteToWideChar(CP_ACP, 0, message.data(), int(message.size()), nullptr, 0);
            std::wstring wide(size, L'\0');
            MultiByteToWideChar(CP_ACP, 0, message.data(), int(message.size()), wide.data(), size);
            return wide;
        }
    }

    std::wstring Text(HWND control)
    {
        const int length = GetWindowTextLengthW(control);
        std::wstring text(length + 1, L'\0');
        text.resize(GetWindowTextW(control, text.data(), length + 1));
        return text;
    }

    struct Window
    {
        HWND handle = nullptr;
        HWND repo = nullptr;
        HWND solution = nullptr;
        HWND choice = nullptr;
        HWND status = nullptr;
        HWND log = nullptr;
        std::mutex buildOutputMutex;
        std::deque<std::wstring> buildOutput;
        bool buildOutputDropped = false;
        // 破棄時は future が先に待機し、ワーカーが使うログキューを先に解放しない。
        std::future<Outcome> job;
        std::stop_source stop;
        fs::path directory;
        std::optional<ProjectSyncPlan> pendingSync;
        std::optional<Configuration> workflow;
        Step step = Step::GitCheck;
        unsigned completed = 0;
        bool jobCancellable = false;
        std::wstring preferredChoice = L"Debug|x64";
        bool closing = false;
        bool logFailed = false;

        HWND Add(const wchar_t* cls, const wchar_t* label, DWORD style, int x, int y,
            int width, int height, int id = 0, DWORD extended = 0) const
        {
            const HWND child = CreateWindowExW(extended, cls, label, WS_CHILD | WS_VISIBLE | style,
                x, y, width, height, handle, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                GetModuleHandleW(nullptr), nullptr);
            if (child == nullptr)
            {
                ThrowWindowsError("Create launcher control");
            }
            SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
            return child;
        }

        void Append(const std::wstring& message, bool persist = true)
        {
            SYSTEMTIME time{};
            GetLocalTime(&time);
            wchar_t timestamp[32];
            swprintf_s(timestamp, L"%04u-%02u-%02u %02u:%02u:%02u", unsigned(time.wYear),
                unsigned(time.wMonth), unsigned(time.wDay), unsigned(time.wHour),
                unsigned(time.wMinute), unsigned(time.wSecond));
            const std::wstring line = std::wstring(timestamp) + L"  " + message + L"\r\n";
            if (GetWindowTextLengthW(log) > 500000)
            {
                SendMessageW(log, EM_SETSEL, 0, 100000);
                SendMessageW(log, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(L""));
            }
            SendMessageW(log, EM_SETSEL, WPARAM(-1), LPARAM(-1));
            SendMessageW(log, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(line.c_str()));
            if (persist == false || directory.empty() || logFailed)
            {
                return;
            }
            try
            {
                fs::create_directories(directory);
                std::ofstream file(directory / L"launcher.log", std::ios::app | std::ios::binary);
                file << winrt::to_string(line);
                file.close();
                logFailed = file.fail();
            }
            catch (...)
            {
                logFailed = true;
            }
            if (logFailed)
            {
                Append(L"Could not write the log file.");
            }
        }

        void SetState(const wchar_t* message, bool busy, bool cancellable = true) const
        {
            SetWindowTextW(status, message);
            for (const int id : {Browse, OpenIde, RepositoryPath, SolutionPath, BuildChoice,
                UpdateGit, SyncProjects, Build, RefreshConfigs, UpdateAndOpen, Setup, Diagnose})
            {
                EnableWindow(GetDlgItem(handle, id), busy == false);
            }
            EnableWindow(GetDlgItem(handle, Cancel), busy && cancellable);
        }

        void Initialize()
        {
            Add(L"STATIC", L"Repository", 0, 20, 22, 90, 20);
            repo = Add(L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL, 112, 18, 474, 26, RepositoryPath, WS_EX_CLIENTEDGE);
            Add(L"BUTTON", L"Browse...", WS_TABSTOP, 598, 18, 92, 26, Browse);
            Add(L"STATIC", L"Solution", 0, 20, 56, 90, 20);
            solution = Add(L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL, 112, 52, 578, 26, SolutionPath, WS_EX_CLIENTEDGE);
            Add(L"STATIC", L"Build config", 0, 20, 90, 90, 20);
            choice = Add(L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST, 112, 86, 180, 140, BuildChoice);
            Add(L"BUTTON", L"Load Configs", WS_TABSTOP, 304, 86, 120, 26, RefreshConfigs);
            Add(L"BUTTON", L"Git Update", WS_TABSTOP, 20, 128, 100, 30, UpdateGit);
            Add(L"BUTTON", L"Sync Projects", WS_TABSTOP, 130, 128, 110, 30, SyncProjects);
            Add(L"BUTTON", L"Build", WS_TABSTOP, 250, 128, 80, 30, Build);
            Add(L"BUTTON", L"Open Visual Studio", WS_TABSTOP, 350, 128, 174, 30, OpenIde);
            Add(L"BUTTON", L"Cancel", WS_TABSTOP, 534, 128, 90, 30, Cancel);
            Add(L"BUTTON", L"Update && Open", WS_TABSTOP, 20, 174, 206, 30, UpdateAndOpen);
            Add(L"BUTTON", L"Setup...", WS_TABSTOP, 236, 174, 92, 30, Setup);
            Add(L"BUTTON", L"Diagnose", WS_TABSTOP, 338, 174, 110, 30, Diagnose);
            status = Add(L"STATIC", L"", 0, 20, 224, 670, 24);
            Add(L"STATIC", L"Log", 0, 20, 256, 90, 20);
            log = Add(L"EDIT", L"", ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL,
                20, 281, 670, 210, LogView, WS_EX_CLIENTEDGE);
            SendMessageW(log, EM_SETLIMITTEXT, 0x7ffffffe, 0);
            SetState(L"Ready", false);
            Apply({});
            try
            {
                directory = SettingsDirectory();
                Apply(LoadConfiguration(directory / L"config.json"));
                if (Text(repo).empty())
                {
                    SetState(L"First run: use Setup to choose your Raven checkout.", false);
                }
            }
            catch (...)
            {
                Append(L"Settings could not be loaded. " + ErrorMessage());
            }
        }

        void Apply(const Configuration& settings)
        {
            SetWindowTextW(repo, settings.repositoryRoot.c_str());
            SetWindowTextW(solution, settings.solutionPath.c_str());
            preferredChoice = settings.configuration + L"|" + settings.platform;
            if (settings.repositoryRoot.empty() == false)
            {
                LoadChoices();
            }
        }

        void LoadChoices()
        {
            const auto current = Text(choice);
            if (current.empty() == false)
            {
                preferredChoice = current;
            }
            SendMessageW(choice, CB_RESETCONTENT, 0, 0);
            const auto values = SolutionConfigurations({Text(repo), Text(solution)});
            for (const auto& value : values)
            {
                SendMessageW(choice, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(value.c_str()));
            }
            const auto index = SendMessageW(choice, CB_FINDSTRINGEXACT, WPARAM(-1),
                reinterpret_cast<LPARAM>(preferredChoice.c_str()));
            SendMessageW(choice, CB_SETCURSEL, index == CB_ERR ? 0 : index, 0);
        }

        Configuration CurrentSettings()
        {
            if (Text(choice).empty())
            {
                LoadChoices();
            }
            const auto pair = Text(choice);
            const auto separator = pair.find(L'|');
            if (separator == std::wstring::npos || directory.empty())
            {
                throw std::runtime_error("Build configuration or settings directory is unavailable.");
            }
            return {Text(repo), Text(solution), pair.substr(0, separator), pair.substr(separator + 1)};
        }

        bool PickFolder() const
        {
            auto dialog = winrt::create_instance<IFileOpenDialog>(CLSID_FileOpenDialog);
            winrt::check_hresult(dialog->SetOptions(FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST));
            const HRESULT result = dialog->Show(handle);
            if (result == HRESULT_FROM_WIN32(ERROR_CANCELLED))
            {
                return false;
            }
            winrt::check_hresult(result);
            winrt::com_ptr<IShellItem> item;
            winrt::check_hresult(dialog->GetResult(item.put()));
            PWSTR value = nullptr;
            winrt::check_hresult(item->GetDisplayName(SIGDN_FILESYSPATH, &value));
            const std::unique_ptr<wchar_t, decltype(&CoTaskMemFree)> owner(value, CoTaskMemFree);
            SetWindowTextW(repo, value);
            return true;
        }

        void SetupWizard()
        {
            if (PickFolder() == false)
            {
                return;
            }
            if (Text(solution).empty())
            {
                SetWindowTextW(solution, L"Root/Root.sln");
            }
            auto settings = CurrentSettings();
            StartJob(L"Validating and saving launcher settings...", true,
                [settings = std::move(settings), file = directory / L"config.json"](std::stop_token stop) mutable
                {
                    ValidateConfiguration(settings, stop);
                    if (stop.stop_requested())
                    {
                        throw std::runtime_error("Setup cancelled.");
                    }
                    SaveConfiguration(file, settings);
                    Outcome outcome;
                    outcome.settings = std::move(settings);
                    outcome.message = L"Setup saved. Use Diagnose to check the installed build tools.";
                    outcome.status = L"Setup saved";
                    outcome.settingsSaved = true;
                    return outcome;
                });
        }

        void StartDiagnostics()
        {
            auto pair = Text(choice);
            if (pair.empty())
            {
                pair = preferredChoice;
            }
            const auto separator = pair.find(L'|');
            Configuration settings{Text(repo), Text(solution)};
            if (separator != std::wstring::npos)
            {
                settings.configuration = pair.substr(0, separator);
                settings.platform = pair.substr(separator + 1);
            }
            StartJob(L"Checking the Raven Launcher environment...", true,
                [settings = std::move(settings)](std::stop_token stop)
                {
                    auto report = DiagnoseEnvironment(settings, stop);
                    Outcome outcome;
                    outcome.message = std::move(report.details);
                    outcome.status = report.passed ? L"Environment checks passed" : L"Environment checks found issues";
                    outcome.showDiagnostics = true;
                    return outcome;
                });
        }

        void StartJob(const wchar_t* description, bool cancellable,
            std::function<Outcome(std::stop_token)> work)
        {
            if (job.valid())
            {
                return;
            }
            stop = std::stop_source{};
            jobCancellable = cancellable;
            const auto token = stop.get_token();
            if (SetTimer(handle, PollJob, 50, nullptr) == 0)
            {
                ThrowWindowsError("SetTimer");
            }
            try
            {
                // UI とワーカーで HWND や設定への可変参照を共有しない。
                job = std::async(std::launch::async, [work = std::move(work), token]
                {
                    try
                    {
                        winrt::init_apartment(winrt::apartment_type::multi_threaded);
                        struct Apartment { ~Apartment() { winrt::uninit_apartment(); } } apartment;
                        return work(token);
                    }
                    catch (...)
                    {
                        Outcome outcome;
                        outcome.message = ErrorMessage();
                        outcome.succeeded = false;
                        return outcome;
                    }
                });
            }
            catch (...)
            {
                KillTimer(handle, PollJob);
                throw;
            }
            const auto progress = workflow ? L"[" + std::to_wstring(completed + 1) + L"/4] " + description :
                std::wstring(description);
            SetState(progress.c_str(), true, cancellable);
            Append(progress);
        }

        void Open()
        {
            auto settings = workflow ? *workflow : CurrentSettings();
            const auto file = directory / L"config.json";
            StartJob(L"Checking the repository and locating Visual Studio...", true,
                [settings = std::move(settings), file](std::stop_token token) mutable
                {
                    const auto sln = ValidateConfiguration(settings, token);
                    const auto ide = FindVisualStudio(settings.repositoryRoot, token);
                    if (token.stop_requested())
                    {
                        Outcome outcome;
                        outcome.message = L"Operation cancelled.";
                        outcome.succeeded = false;
                        return outcome;
                    }
                    SaveConfiguration(file, settings);
                    LaunchProcess(ide, {sln.native()}, settings.repositoryRoot);
                    return Outcome{std::move(settings), L"Visual Studio launch requested.", true};
                });
        }

        void QueueBuildLine(std::wstring line)
        {
            // 詳細はワーカー側のログに残し、GUI が一度に処理する表示行数を制限する。
            const std::lock_guard lock(buildOutputMutex);
            if (buildOutput.size() >= 256)
            {
                buildOutput.pop_front();
                buildOutputDropped = true;
            }
            if (line.size() > 4096)
            {
                line.resize(4096);
                line += L" ...";
            }
            buildOutput.push_back(std::move(line));
        }

        void FlushBuildLines()
        {
            std::deque<std::wstring> lines;
            bool dropped = false;
            {
                const std::lock_guard lock(buildOutputMutex);
                lines.swap(buildOutput);
                dropped = std::exchange(buildOutputDropped, false);
            }
            if (lines.empty())
            {
                return;
            }
            std::wstring message;
            if (dropped)
            {
                message = L"Some lines were omitted from the display. See the build log.\r\n";
            }
            for (const auto& line : lines)
            {
                if (message.empty() == false && message.ends_with(L"\r\n") == false)
                {
                    message += L"\r\n";
                }
                message += line;
            }
            Append(message, false);
        }

        void StartBuild()
        {
            Configuration settings;
            try
            {
                settings = workflow ? *workflow : CurrentSettings();
            }
            catch (...)
            {
                if (workflow)
                {
                    throw;
                }
                SetState(L"Build failed", false);
                Append(L"Build failed. " + ErrorMessage());
                return;
            }
            StartJob(L"Locating MSBuild and building the selected solution...", true,
                [this, settings = std::move(settings), settingsDir = directory](std::stop_token token)
                {
                    Outcome outcome;
                    try
                    {
                        auto result = BuildSolution(settings, settingsDir, token,
                            [this](std::wstring line) { QueueBuildLine(std::move(line)); });
                        outcome.status = std::move(result.status);
                        outcome.message = std::move(result.details);
                        outcome.succeeded = result.succeeded;
                    }
                    catch (...)
                    {
                        outcome.status = token.stop_requested() ? L"Build cancelled" : L"Build failed";
                        outcome.message = outcome.status + L". " + ErrorMessage();
                        outcome.succeeded = false;
                    }
                    return outcome;
                });
        }

        void CheckUpdate()
        {
            StartJob(L"Checking Git status and fetching the upstream branch...", true,
                [root = workflow ? workflow->repositoryRoot : fs::path(Text(repo))](std::stop_token token)
                {
                    auto plan = CheckGitUpdate(root, token);
                    auto message = L"Branch: " + plan.branch + L"\r\nUpstream: " + plan.upstream +
                        L"\r\nCurrent commit: " + plan.before + L"\r\nFetched commit: " + plan.target;
                    if (plan.before == plan.target)
                    {
                        return Outcome{{}, message + L"\r\nThe branch is already up to date."};
                    }
                    return Outcome{{}, std::move(message), false, std::move(plan)};
                });
        }

        void PreviewSync()
        {
            if (directory.empty())
            {
                throw std::runtime_error("The settings directory is unavailable for project sync backups.");
            }
            StartJob(L"Scanning Visual Studio project registrations...", true,
                [root = workflow ? workflow->repositoryRoot : fs::path(Text(repo)), review = workflow.has_value()]
                (std::stop_token token)
                {
                    auto plan = PreviewProjectSync(root, token);
                    auto message = plan.preview;
                    if (plan.files.empty() && (review == false || plan.needsReview == false))
                    {
                        return Outcome{{}, std::move(message)};
                    }
                    return Outcome{{}, std::move(message), false, {}, std::move(plan)};
                });
        }

        bool ConfirmGit(const std::wstring& preview) const
        {
            return MessageBoxW(handle, (preview + L"\r\n\r\nApply this fast-forward update?").c_str(),
                L"Confirm Git Update", MB_YESNO | MB_DEFBUTTON2 | MB_ICONQUESTION) == IDYES;
        }

        void StartGitApply(GitUpdatePlan plan)
        {
            // 作業ツリー更新の途中で強制終了しないよう、反映中はユーザーキャンセルを受け付けない。
            StartJob(L"Applying the confirmed fast-forward update...", false,
                [plan = std::move(plan)](std::stop_token)
                {
                    return Outcome{{}, ApplyGitUpdate(plan)};
                });
        }

        bool ConfirmSync(const ProjectSyncPlan& plan) const
        {
            const auto message = plan.files.empty() ?
                L"Continue after reviewing the manual items in the log? No project files will be changed." :
                L"Apply the previewed changes to " + std::to_wstring(plan.files.size()) +
                    L" project files? A backup will be created before replacing them.";
            return MessageBoxW(handle, message.c_str(), L"Confirm Project Sync",
                MB_YESNO | MB_DEFBUTTON2 | MB_ICONQUESTION) == IDYES;
        }

        void StartSyncApply(ProjectSyncPlan plan)
        {
            StartJob(L"Applying the confirmed project changes...", false,
                [plan = std::move(plan), backups = directory / L"sync-backups"](std::stop_token)
                {
                    auto result = ApplyProjectSync(plan, backups);
                    Outcome outcome;
                    outcome.succeeded = result.succeeded;
                    outcome.message = std::move(result.details);
                    return outcome;
                });
        }

        void StartWorkflow()
        {
            if (workflow)
            {
                if (job.valid() || pendingSync.has_value() == false)
                {
                    return;
                }
                auto plan = std::move(*pendingSync);
                pendingSync.reset();
                if (ConfirmSync(plan) == false)
                {
                    FinishWorkflow(L"Project sync approval declined.");
                    return;
                }
                SetWindowTextW(GetDlgItem(handle, UpdateAndOpen), L"Update && Open");
                if (plan.files.empty() == false)
                {
                    step = Step::SyncApply;
                    StartSyncApply(std::move(plan));
                    return;
                }
                BeginWorkflowBuild();
                return;
            }

            auto settings = CurrentSettings();
            const auto summary = L"Git update, project sync, build, and open Visual Studio.\r\n\r\n" +
                settings.solutionPath.native() + L" (" + settings.configuration + L"|" + settings.platform +
                L")\r\nGit and project file changes require separate approval. Start?";
            if (MessageBoxW(handle, summary.c_str(), L"Update and Open",
                MB_YESNO | MB_DEFBUTTON2 | MB_ICONQUESTION) != IDYES)
            {
                return;
            }
            pendingSync.reset();
            SetWindowTextW(GetDlgItem(handle, SyncProjects), L"Sync Projects");
            SetWindowTextW(GetDlgItem(handle, UpdateAndOpen), L"Update && Open");
            workflow = std::move(settings);
            completed = 0;
            step = Step::GitCheck;
            CheckUpdate();
        }

        void FinishWorkflow(std::wstring_view reason)
        {
            const bool success = completed == 4;
            workflow.reset();
            pendingSync.reset();
            SetWindowTextW(GetDlgItem(handle, UpdateAndOpen), success ? L"Update && Open" : L"Retry Update && Open");
            SetState(success ? L"Update and Open completed." : L"Update and Open stopped. See log.", false);
            Append(std::wstring(reason) + L" Completed steps: " + std::to_wstring(completed) + L"/4.");
        }

        void BeginWorkflowSync()
        {
            completed = 1;
            step = Step::SyncCheck;
            PreviewSync();
        }

        void BeginWorkflowBuild()
        {
            completed = 2;
            step = Step::Build;
            Append(L"Project review completed.");
            StartBuild();
        }

        void AdvanceWorkflow(Outcome result)
        {
            // 失敗・キャンセルの判定を工程の分岐より先に行い、後続の副作用を防ぐ。
            if (stop.stop_requested() && result.opened == false)
            {
                FinishWorkflow(L"Update and Open cancelled.");
                return;
            }
            if (result.succeeded == false)
            {
                FinishWorkflow(L"Update and Open stopped at step " + std::to_wstring(completed + 1) + L".");
                return;
            }
            switch (step)
            {
            case Step::GitCheck:
                if (result.gitPlan)
                {
                    if (ConfirmGit(result.message) == false)
                    {
                        FinishWorkflow(L"Git update approval declined.");
                        return;
                    }
                    step = Step::GitApply;
                    StartGitApply(std::move(*result.gitPlan));
                    return;
                }
                BeginWorkflowSync();
                return;
            case Step::GitApply:
                BeginWorkflowSync();
                return;
            case Step::SyncCheck:
                if (result.syncPlan)
                {
                    pendingSync = std::move(result.syncPlan);
                    SetState(L"[2/4] Review the project preview in the log before continuing.", true);
                    SetWindowTextW(GetDlgItem(handle, UpdateAndOpen), L"Review && Continue...");
                    EnableWindow(GetDlgItem(handle, UpdateAndOpen), TRUE);
                    return;
                }
                BeginWorkflowBuild();
                return;
            case Step::SyncApply:
                BeginWorkflowBuild();
                return;
            case Step::Build:
                completed = 3;
                step = Step::Open;
                Open();
                return;
            case Step::Open:
                completed = 4;
                FinishWorkflow(L"Update and Open completed.");
                return;
            }
        }

        void RequestCancel()
        {
            if (workflow && job.valid() == false)
            {
                FinishWorkflow(L"Update and Open cancelled before project changes.");
                return;
            }
            if (job.valid() && jobCancellable)
            {
                stop.request_stop();
                SetWindowTextW(status, L"Cancelling...");
            }
        }

        void Poll()
        {
            FlushBuildLines();
            if (job.valid() == false || job.wait_for(0ms) != std::future_status::ready)
            {
                return;
            }
            KillTimer(handle, PollJob);
            auto result = job.get();
            FlushBuildLines();
            Append(result.message);
            if (closing)
            {
                if (workflow)
                {
                    FinishWorkflow(L"Update and Open stopped because the launcher was closed.");
                }
                DestroyWindow(handle);
                return;
            }
            if (workflow)
            {
                AdvanceWorkflow(std::move(result));
                return;
            }
            SetState(result.status.empty() ? (result.opened ? L"Launch requested" : L"Operation ended. See log.") :
                result.status.c_str(), false);
            if (result.opened || result.settingsSaved)
            {
                Apply(result.settings);
            }
            if (result.showDiagnostics && stop.stop_requested() == false)
            {
                MessageBoxW(handle, result.message.c_str(), L"Environment Diagnostics", MB_OK | MB_ICONINFORMATION);
            }
            if (result.syncPlan)
            {
                if (stop.stop_requested())
                {
                    Append(L"Project sync preview cancelled.");
                    return;
                }
                SetState(L"Review the project sync preview in the log.", false);
                // ログの全行を読んでから承認できるよう、プレビューと反映を別の操作にする。
                pendingSync = std::move(result.syncPlan);
                SetWindowTextW(GetDlgItem(handle, SyncProjects), L"Apply Sync...");
                return;
            }
            if (result.gitPlan)
            {
                if (stop.stop_requested() || ConfirmGit(result.message) == false)
                {
                    Append(L"Git update cancelled before applying changes.");
                    return;
                }
                StartGitApply(std::move(*result.gitPlan));
            }
        }

        void Sync()
        {
            if (pendingSync.has_value() == false)
            {
                PreviewSync();
                return;
            }
            auto plan = std::move(*pendingSync);
            pendingSync.reset();
            SetWindowTextW(GetDlgItem(handle, SyncProjects), L"Sync Projects");
            if (ConfirmSync(plan) == false)
            {
                Append(L"Project sync cancelled before applying changes.");
                return;
            }
            StartSyncApply(std::move(plan));
        }
    };

    LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
    {
        if (message == WM_NCCREATE)
        {
            const auto create = reinterpret_cast<CREATESTRUCTW*>(lparam);
            auto* state = static_cast<Window*>(create->lpCreateParams);
            state->handle = window;
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
        }
        auto* state = reinterpret_cast<Window*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (state == nullptr)
        {
            return DefWindowProcW(window, message, wparam, lparam);
        }
        try
        {
            switch (message)
            {
            case WM_CREATE:
                state->Initialize();
                return 0;
            case WM_COMMAND:
                if ((LOWORD(wparam) == RepositoryPath || LOWORD(wparam) == SolutionPath) && HIWORD(wparam) == EN_CHANGE)
                {
                    const auto current = Text(state->choice);
                    if (current.empty() == false)
                    {
                        state->preferredChoice = current;
                    }
                    SendMessageW(state->choice, CB_RESETCONTENT, 0, 0);
                }
                if (LOWORD(wparam) == RepositoryPath && HIWORD(wparam) == EN_CHANGE && state->pendingSync)
                {
                    state->pendingSync.reset();
                    SetWindowTextW(GetDlgItem(window, SyncProjects), L"Sync Projects");
                }
                if (HIWORD(wparam) == BN_CLICKED)
                {
                    switch (LOWORD(wparam))
                    {
                    case Browse: state->PickFolder(); return 0;
                    case OpenIde: state->Open(); return 0;
                    case UpdateGit: state->CheckUpdate(); return 0;
                    case SyncProjects: state->Sync(); return 0;
                    case Build: state->StartBuild(); return 0;
                    case RefreshConfigs: state->LoadChoices(); return 0;
                    case UpdateAndOpen: state->StartWorkflow(); return 0;
                    case Setup: state->SetupWizard(); return 0;
                    case Diagnose: state->StartDiagnostics(); return 0;
                    case Cancel: state->RequestCancel(); return 0;
                    }
                }
                break;
            case WM_TIMER:
                if (wparam == PollJob)
                {
                    state->Poll();
                    return 0;
                }
                break;
            case WM_CLOSE:
                if (state->job.valid())
                {
                    state->closing = true;
                    if (state->jobCancellable)
                    {
                        state->stop.request_stop();
                    }
                    SetWindowTextW(state->status, L"Waiting for the current operation before closing...");
                    return 0;
                }
                if (state->workflow)
                {
                    state->FinishWorkflow(L"Update and Open stopped because the launcher was closed.");
                }
                break;
            case WM_DESTROY:
                PostQuitMessage(0);
                return 0;
            }
        }
        catch (...)
        {
            const auto error = ErrorMessage();
            if (message == WM_CREATE)
            {
                MessageBoxW(window, error.c_str(), L"Raven Launcher", MB_ICONERROR);
                return -1;
            }
            state->Append(error);
            if (state->workflow)
            {
                state->FinishWorkflow(L"Update and Open stopped.");
            }
        }
        return DefWindowProcW(window, message, wparam, lparam);
    }
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show)
{
    try
    {
        winrt::init_apartment(winrt::apartment_type::single_threaded);
        struct Apartment { ~Apartment() { winrt::uninit_apartment(); } } apartment;
        constexpr wchar_t windowClass[] = L"RavenLauncherWindow";
        WNDCLASSW definition{};
        definition.lpfnWndProc = WindowProcedure;
        definition.hInstance = instance;
        definition.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        definition.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
        definition.lpszClassName = windowClass;
        if (RegisterClassW(&definition) == 0)
        {
            ThrowWindowsError("RegisterClass");
        }
        Window state;
        constexpr DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
        RECT bounds{0, 0, 710, 511};
        AdjustWindowRect(&bounds, style, FALSE);
        const HWND window = CreateWindowExW(WS_EX_CONTROLPARENT, windowClass, L"Raven Launcher", style,
            CW_USEDEFAULT, CW_USEDEFAULT, bounds.right - bounds.left, bounds.bottom - bounds.top,
            nullptr, nullptr, instance, &state);
        if (window == nullptr)
        {
            return 1;
        }
        ShowWindow(window, show);
        MSG message{};
        BOOL result = 0;
        while ((result = GetMessageW(&message, nullptr, 0, 0)) > 0)
        {
            if (IsDialogMessageW(window, &message) == FALSE)
            {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        }
        state.stop.request_stop();
        return result == -1 ? 1 : static_cast<int>(message.wParam);
    }
    catch (...)
    {
        MessageBoxW(nullptr, ErrorMessage().c_str(), L"Raven Launcher", MB_ICONERROR);
        return 1;
    }
}
