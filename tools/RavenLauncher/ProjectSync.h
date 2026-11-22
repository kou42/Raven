#pragma once

#include <filesystem>
#include <stop_token>
#include <string>
#include <vector>

namespace RavenLauncher
{
    struct SyncFile
    {
        std::filesystem::path path;
        std::string before;
        std::string after;

        bool operator==(const SyncFile&) const = default;
    };

    struct ProjectSyncPlan
    {
        std::filesystem::path root;
        std::vector<SyncFile> files;
        std::wstring preview;
        bool needsReview = false;

        bool operator==(const ProjectSyncPlan&) const = default;
    };

    ProjectSyncPlan PreviewProjectSync(const std::filesystem::path& root, std::stop_token stop);
    struct ProjectSyncResult
    {
        bool succeeded;
        std::wstring details;
    };

    ProjectSyncResult ApplyProjectSync(const ProjectSyncPlan& plan, const std::filesystem::path& backupRoot);
}
