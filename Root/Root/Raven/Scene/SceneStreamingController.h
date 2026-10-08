#pragma once

#include "Raven/Scene/SceneManager.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace Raven
{

class SceneFactory;

// World側が計算した「現在必要なStage ID集合」をAdditive Sceneの差分へ変換します。
// 距離判定、Portal、メモリBudget等のPolicyはGame側に残し、この型はLifetime操作だけを担当します。
class SceneStreamingController
{
public:
    SceneStreamingController(SceneManager& sceneManager, SceneFactory& sceneFactory);

    bool LoadStage(const std::string& stageID);
    bool UnloadStage(const std::string& stageID);
    bool SynchronizeStages(const std::vector<std::string>& desiredStageIDs);

    SceneInstanceID GetStageSceneID(const std::string& stageID) const;
    std::size_t GetTrackedStageCount() const { return m_StageScenes.size(); }
    void Clear();

private:
    SceneManager& m_SceneManager;
    SceneFactory& m_SceneFactory;
    std::unordered_map<std::string, SceneInstanceID> m_StageScenes;
};

} // namespace Raven
