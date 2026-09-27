#pragma once

#include "Raven/UI/Navigation/UIScreen.h"

#include <string>

namespace Raven
{

class UIPanel;

// Async Scene Preparation中に表示するRuntime Loading UIです。
// Transition Controllerから進捗/結果だけを受け取り、Scene生成や遷移状態そのものは所有しません。
class LoadingScreen final : public UIScreen
{
public:
    LoadingScreen();

    void SetProgress(float progress);
    float GetProgress() const { return m_Progress; }

    void SetMessage(std::string message);
    const std::string& GetMessage() const { return m_Message; }

    void SetLoadError(std::string message);
    bool HasLoadError() const { return m_HasLoadError; }

private:
    void RefreshVisualState();

private:
    UIPanel* m_ProgressFill = nullptr;
    UIPanel* m_StatusIndicator = nullptr;
    float m_Progress = 0.0f;
    std::string m_Message = "Loading...";
    bool m_HasLoadError = false;
};

} // namespace Raven
