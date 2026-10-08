#include "Raven/Scene/SceneStreamingController.h"

#include "Raven/Scene/SceneFactory.h"

#include <unordered_set>

namespace Raven
{

SceneStreamingController::SceneStreamingController(
    SceneManager& sceneManager, SceneFactory& sceneFactory)
    : m_SceneManager(sceneManager)
    , m_SceneFactory(sceneFactory)
{
}

bool SceneStreamingController::LoadStage(const std::string& stageID)
{
    if (stageID.empty() == true || m_StageScenes.find(stageID) != m_StageScenes.end())
    {
        return false;
    }

    Scope<Scene> scene = m_SceneFactory.Create(stageID);
    if (scene == nullptr)
    {
        return false;
    }

    const SceneInstanceID sceneID = m_SceneManager.LoadSceneAdditive(std::move(scene));
    if (sceneID == InvalidSceneInstanceID)
    {
        return false;
    }
    m_StageScenes.emplace(stageID, sceneID);
    return true;
}

bool SceneStreamingController::UnloadStage(const std::string& stageID)
{
    const auto it = m_StageScenes.find(stageID);
    if (it == m_StageScenes.end())
    {
        return false;
    }

    const bool requested = m_SceneManager.UnloadScene(it->second);
    if (requested == true)
    {
        m_StageScenes.erase(it);
    }
    return requested;
}

bool SceneStreamingController::SynchronizeStages(
    const std::vector<std::string>& desiredStageIDs)
{
    std::unordered_set<std::string> desired;
    std::vector<std::string> orderedDesired;
    for (const std::string& stageID : desiredStageIDs)
    {
        if (stageID.empty() == true)
        {
            return false;
        }
        if (desired.insert(stageID).second == true)
        {
            orderedDesired.push_back(stageID);
        }
    }

    // 途中までLoadしてから未知IDで失敗するとWorld集合が半端になるため、先に全IDを検証します。
    for (const std::string& stageID : orderedDesired)
    {
        if (m_StageScenes.find(stageID) == m_StageScenes.end()
            && m_SceneFactory.Contains(stageID) == false)
        {
            return false;
        }
    }

    for (const std::string& stageID : orderedDesired)
    {
        if (m_StageScenes.find(stageID) != m_StageScenes.end())
        {
            continue;
        }
        if (LoadStage(stageID) == false)
        {
            return false;
        }
    }

    std::vector<std::string> unloadList;
    for (const auto& entry : m_StageScenes)
    {
        if (desired.find(entry.first) == desired.end())
        {
            unloadList.push_back(entry.first);
        }
    }
    for (const std::string& stageID : unloadList)
    {
        if (UnloadStage(stageID) == false)
        {
            return false;
        }
    }
    return true;
}

SceneInstanceID SceneStreamingController::GetStageSceneID(const std::string& stageID) const
{
    const auto it = m_StageScenes.find(stageID);
    return it != m_StageScenes.end() ? it->second : InvalidSceneInstanceID;
}

void SceneStreamingController::Clear()
{
    std::vector<std::string> stageIDs;
    stageIDs.reserve(m_StageScenes.size());
    for (const auto& entry : m_StageScenes)
    {
        stageIDs.push_back(entry.first);
    }
    for (const std::string& stageID : stageIDs)
    {
        UnloadStage(stageID);
    }
}

} // namespace Raven
