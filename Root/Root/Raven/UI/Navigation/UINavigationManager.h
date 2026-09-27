#pragma once

#include "Raven/Core/Base.h"

#include <cstddef>
#include <vector>

namespace Raven
{

class UIContext;
class UIScreen;

// Sceneとは独立したUI Screen Stackを管理します。
// Push時は現在画面をPauseし、新画面を最前面へ追加します。
// Pop時は最前面画面を破棄し、一つ下の画面をResumeします。
class UINavigationManager
{
public:
    explicit UINavigationManager(UIContext& context);
    ~UINavigationManager();

    UINavigationManager(const UINavigationManager&) = delete;
    UINavigationManager& operator=(const UINavigationManager&) = delete;

    bool PushScreen(Scope<UIScreen> screen);
    Scope<UIScreen> PopScreen();
    bool ReplaceScreen(Scope<UIScreen> screen);
    void Clear();

    UIScreen* GetTopScreen();
    const UIScreen* GetTopScreen() const;
    std::size_t GetScreenCount() const { return m_Screens.size(); }
    bool IsEmpty() const { return m_Screens.empty(); }

private:
    bool AttachScreen(UIScreen& screen);
    bool DetachScreen(UIScreen& screen);

private:
    UIContext& m_Context;
    std::vector<Scope<UIScreen>> m_Screens;
};

} // namespace Raven
