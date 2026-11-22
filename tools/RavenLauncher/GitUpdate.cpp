#include "GitUpdate.h"
#include "ProcessRunner.h"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <winrt/base.h>

namespace RavenLauncher
{
    namespace fs = std::filesystem;
    using namespace std::chrono_literals;

    namespace
    {
        struct Git
        {
            fs::path executable;
            fs::path root;
            std::stop_token stop;

            ProcessResult Run(std::vector<std::wstring> arguments, std::chrono::seconds timeout = 15s) const
            {
                arguments.insert(arguments.begin(), {L"-c", L"credential.interactive=never", L"-c", L"core.quotePath=false"});
                return RunProcess(executable, arguments, root, stop, timeout);
            }

            std::wstring Read(std::vector<std::wstring> arguments, const char* failure) const
            {
                auto result = Run(std::move(arguments));
                if (result.exitCode != 0)
                {
                    throw std::runtime_error(failure);
                }
                const auto last = result.output.find_last_not_of("\r\n");
                result.output.resize(last == std::string::npos ? 0 : last + 1);
                return winrt::to_hstring(result.output).c_str();
            }
        };

        Git OpenGit(const fs::path& directory, std::stop_token stop)
        {
            auto executable = FindGit();
            auto root = FindGitRoot(executable, directory, stop);
            return {std::move(executable), std::move(root), stop};
        }

        GitUpdatePlan Inspect(const Git& git)
        {
            for (const auto* marker : {L"MERGE_HEAD", L"CHERRY_PICK_HEAD", L"REVERT_HEAD",
                L"rebase-merge", L"rebase-apply", L"sequencer", L"BISECT_START"})
            {
                const auto relative = git.Read({L"rev-parse", L"--git-path", marker}, "Cannot inspect Git operation state.");
                if (fs::exists(git.root / relative))
                {
                    throw std::runtime_error("Finish the in-progress Git operation before updating.");
                }
            }
            if (git.Read({L"status", L"--porcelain=v1", L"--untracked-files=all", L"--ignore-submodules=none"},
                "Cannot inspect the Git working tree.").empty() == false)
            {
                throw std::runtime_error("The working tree has changes. Commit or set them aside before updating.");
            }

            const auto branchRef = git.Read({L"symbolic-ref", L"--quiet", L"HEAD"},
                "HEAD is detached. Select a branch before updating.");
            constexpr std::wstring_view branchPrefix = L"refs/heads/";
            if (branchRef.starts_with(branchPrefix) == false)
            {
                throw std::runtime_error("HEAD does not refer to a local branch.");
            }
            const auto branch = branchRef.substr(branchPrefix.size());
            const auto upstream = git.Read({L"rev-parse", L"--symbolic-full-name", L"@{upstream}"},
                "The current branch has no available upstream.");
            const auto key = L"branch." + branch + L".";
            const auto remote = git.Read({L"config", L"--get", key + L"remote"}, "No upstream remote is configured.");
            const auto remoteRef = git.Read({L"config", L"--get-all", key + L"merge"}, "No upstream branch is configured.");
            if (upstream.starts_with(L"refs/remotes/") == false || remote == L"." ||
                remote.empty() || remote.starts_with(L"-") || remoteRef.starts_with(L"refs/heads/") == false ||
                git.Run({L"check-ref-format", remoteRef}).exitCode != 0)
            {
                throw std::runtime_error("The upstream must identify one remote branch.");
            }
            const auto head = git.Read({L"rev-parse", L"--verify", L"HEAD^{commit}"}, "Cannot resolve the current commit.");
            const auto target = git.Read({L"rev-parse", L"--verify", upstream + L"^{commit}"}, "Cannot resolve the upstream commit.");
            return {git.root, branch, upstream, remote, remoteRef, head, target};
        }

        bool IsAncestor(const Git& git, const std::wstring& ancestor, const std::wstring& descendant)
        {
            const auto result = git.Run({L"merge-base", L"--is-ancestor", ancestor, descendant});
            if (result.exitCode > 1)
            {
                throw std::runtime_error("Cannot compare branch history.");
            }
            return result.exitCode == 0;
        }

        std::vector<std::wstring> ReadPaths(const Git& git, std::vector<std::wstring> arguments)
        {
            const auto result = git.Run(std::move(arguments));
            if (result.exitCode != 0)
            {
                throw std::runtime_error("Cannot check whether the update would overwrite ignored files.");
            }
            std::vector<std::wstring> paths;
            size_t begin = 0;
            while (begin < result.output.size())
            {
                const auto end = result.output.find('\0', begin);
                if (end == std::string::npos)
                {
                    throw std::runtime_error("Git returned an incomplete file list.");
                }
                paths.emplace_back(winrt::to_hstring(std::string_view(result.output).substr(begin, end - begin)).c_str());
                begin = end + 1;
            }
            return paths;
        }

        bool AtOrBelow(std::wstring_view path, std::wstring_view parent)
        {
            return path.size() >= parent.size() &&
                (path.size() == parent.size() || path[parent.size()] == L'/') &&
                CompareStringOrdinal(path.data(), int(parent.size()), parent.data(), int(parent.size()), TRUE) == CSTR_EQUAL;
        }

        void CheckIgnoredFiles(const Git& git, const GitUpdatePlan& plan)
        {
            // git pull は無視対象を上書きできるため、追加パスとその親子にある無視対象を事前に検出する。
            const auto added = ReadPaths(git, {L"diff", L"--name-only", L"--diff-filter=A", L"--no-renames",
                L"--no-ext-diff", L"-z", plan.before, plan.target, L"--"});
            if (added.empty())
            {
                return;
            }
            const auto ignored = ReadPaths(git, {L"ls-files", L"--others", L"--ignored", L"--exclude-standard", L"-z"});
            for (const auto& path : added)
            {
                for (const auto& existing : ignored)
                {
                    if (AtOrBelow(path, existing) || AtOrBelow(existing, path))
                    {
                        throw std::runtime_error("The update would overwrite an ignored file. Move it before updating.");
                    }
                }
            }
        }

        [[noreturn]] void FetchError(std::string errors)
        {
            // stderr には資格情報付き URL が含まれ得るため、分類結果だけを表示する。
            std::transform(errors.begin(), errors.end(), errors.begin(),
                [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
            if (errors.contains("authentication") || errors.contains("permission denied") ||
                errors.contains("could not read username") || errors.contains("terminal prompts disabled") ||
                errors.contains("authorization"))
            {
                throw std::runtime_error("Git fetch authentication failed. Check the remote credentials.");
            }
            if (errors.contains("couldn't find remote ref"))
            {
                throw std::runtime_error("The tracked branch no longer exists on the remote.");
            }
            if (errors.contains("resolve host") || errors.contains("connect") ||
                errors.contains("timed out") || errors.contains("network"))
            {
                throw std::runtime_error("Cannot connect to the Git remote. Check the network and remote settings.");
            }
            throw std::runtime_error("Git fetch failed. Check the remote configuration and access.");
        }
    }

    fs::path FindGit()
    {
        const DWORD size = GetEnvironmentVariableW(L"PATH", nullptr, 0);
        std::wstring path(size, L'\0');
        const DWORD length = GetEnvironmentVariableW(L"PATH", path.data(), size);
        if (length == 0 || length >= size)
        {
            throw std::runtime_error("PATH is unavailable.");
        }
        wchar_t git[32768];
        const DWORD found = SearchPathW(path.c_str(), L"git.exe", nullptr, DWORD(std::size(git)), git, nullptr);
        if (found == 0 || found >= std::size(git))
        {
            throw std::runtime_error("Git was not found. Install Git for Windows and add it to PATH.");
        }
        return git;
    }

    fs::path FindGitRoot(const fs::path& git, const fs::path& directory, std::stop_token stop)
    {
        if (directory.is_absolute() == false || fs::is_directory(directory) == false)
        {
            throw std::runtime_error("Select a valid repository folder.");
        }
        const auto root = Git{git, directory, stop}.Read({L"rev-parse", L"--show-toplevel"},
            "The selected folder is not a Git worktree.");
        return fs::canonical(root);
    }

    GitUpdatePlan CheckGitUpdate(const fs::path& directory, std::stop_token stop)
    {
        const auto git = OpenGit(directory, stop);
        const auto initial = Inspect(git);
        const auto result = git.Run({L"fetch", L"--no-tags", L"--no-prune", L"--no-recurse-submodules",
            L"--no-write-fetch-head", L"--refmap=", L"--", initial.remote,
            L"+" + initial.remoteRef + L":" + initial.upstream}, 120s);
        if (result.exitCode != 0)
        {
            FetchError(result.errors);
        }
        auto plan = Inspect(git);
        auto expected = initial;
        expected.target = plan.target;
        if (expected != plan)
        {
            throw std::runtime_error("Git state changed during fetch. Start the update again.");
        }
        if (plan.before != plan.target && IsAncestor(git, plan.before, plan.target) == false)
        {
            if (IsAncestor(git, plan.target, plan.before))
            {
                throw std::runtime_error("The local branch is ahead of its upstream. No update applied.");
            }
            throw std::runtime_error("The branch and its upstream have diverged. Resolve the history before updating.");
        }
        CheckIgnoredFiles(git, plan);
        return plan;
    }

    std::wstring ApplyGitUpdate(const GitUpdatePlan& plan)
    {
        const auto git = OpenGit(plan.root, {});
        if (Inspect(git) != plan || IsAncestor(git, plan.before, plan.target) == false)
        {
            throw std::runtime_error("Git state changed after confirmation. Start the update again.");
        }
        if (plan.before == plan.target)
        {
            return L"The branch is already up to date.";
        }
        CheckIgnoredFiles(git, plan);
        // 承認後のリモート移動で別コミットを取り込まないよう、取得済みのハッシュをローカルから pull する。
        const auto result = git.Run({L"-c", L"protocol.allow=never", L"-c", L"protocol.file.allow=always",
            L"pull", L"--ff-only", L"--no-rebase", L"--no-autostash", L"--no-squash",
            L"--no-recurse-submodules", L"--no-tags", L"--", L".", plan.target}, 120s);
        if (result.exitCode != 0)
        {
            throw std::runtime_error("Fast-forward failed. Check the working tree and Git state before retrying.");
        }
        const auto after = git.Read({L"rev-parse", L"--verify", L"HEAD^{commit}"}, "Cannot read HEAD after the update.");
        if (after != plan.target)
        {
            throw std::runtime_error("HEAD changed during the update. Inspect the repository before retrying.");
        }
        auto files = git.Read({L"diff", L"--name-status", L"--no-ext-diff", plan.before, after, L"--"},
            "The update completed, but Git could not list the changed files.");
        for (size_t index = files.find(L'\n'); index != std::wstring::npos; index = files.find(L'\n', index + 2))
        {
            files.insert(index, 1, L'\r');
        }
        return L"Updated " + plan.branch + L" from " + plan.before + L" to " + after +
            (files.empty() ? L"\r\nNo file changes." : L"\r\nChanged files:\r\n" + files);
    }
}
