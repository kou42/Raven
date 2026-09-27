#include "Raven/UI/Navigation/UINavigationManager.h"

#include "Raven/UI/Core/UIContext.h"
#include "Raven/UI/Navigation/UIScreen.h"

namespace Raven
{

UINavigationManager::UINavigationManager(UIContext& context)
    : m_Context(context)
{
}

UINavigationManager::~UINavigationManager()
{
    Clear();
}

bool UINavigationManager::PushScreen(Scope<UIScreen> screen)
{
    if (screen == nullptr)
    {
        return false;
    }

    UIScreen* previous = GetTopScreen();
    if (previous != nullptr)
    {
        previous->OnPause();
    }

    if (AttachScreen(*screen) == false)
    {
        if (previous != nullptr)
        {
            previous->OnResume();
        }
        return false;
    }

    screen->OnEnter();
    m_Screens.push_back(std::move(screen));
    return true;
}

Scope<UIScreen> UINavigationManager::PopScreen()
{
    if (m_Screens.empty())
    {
        return nullptr;
    }

    Scope<UIScreen> screen = std::move(m_Screens.back());
    m_Screens.pop_back();

    screen->OnExit();
    DetachScreen(*screen);

    UIScreen* resumed = GetTopScreen();
    if (resumed != nullptr)
    {
        resumed->OnResume();
    }

    return screen;
}

bool UINavigationManager::ReplaceScreen(Scope<UIScreen> screen)
{
    if (screen == nullptr)
    {
        return false;
    }

    // Replaceは一つ下のScreenを一時的にResumeさせません。
    // Topだけを直接交換し、Pause/Resumeの不要な副作用を避けます。
    Scope<UIScreen> previous;
    if (m_Screens.empty() == false)
    {
        previous = std::move(m_Screens.back());
        m_Screens.pop_back();
        previous->OnExit();
        DetachScreen(*previous);
    }

    if (AttachScreen(*screen) == false)
    {
        // 新Screenを接続できなかった場合は旧Topを可能な限り復元します。
        if (previous != nullptr && AttachScreen(*previous) == true)
        {
            previous->OnEnter();
            m_Screens.push_back(std::move(previous));
        }
        return false;
    }

    screen->OnEnter();
    m_Screens.push_back(std::move(screen));
    return true;
}

void UINavigationManager::Clear()
{
    // Topから順に外すことでPainter's OrderとNavigation Stackの順序を一致させます。
    while (m_Screens.empty() == false)
    {
        Scope<UIScreen> screen = std::move(m_Screens.back());
        m_Screens.pop_back();
        screen->OnExit();
        DetachScreen(*screen);
    }
}

UIScreen* UINavigationManager::GetTopScreen()
{
    return m_Screens.empty() ? nullptr : m_Screens.back().get();
}

const UIScreen* UINavigationManager::GetTopScreen() const
{
    return m_Screens.empty() ? nullptr : m_Screens.back().get();
}

bool UINavigationManager::AttachScreen(UIScreen& screen)
{
    Scope<UIElement> root = screen.ReleaseRootElement();
    if (root == nullptr)
    {
        return false;
    }

    UIElement* attached = m_Context.GetRootElement().AddChild(std::move(root));
    if (attached == nullptr)
    {
        return false;
    }

    return true;
}

bool UINavigationManager::DetachScreen(UIScreen& screen)
{
    UIElement* root = nullptr;
    for (const auto& child : m_Context.GetRootElement().GetChildren())
    {
        // UIScreenのRootはAttach中だけManager側Treeが所有するため、
        // Screen自身のRoot参照ではなく、Stack順に対応するRootを復元する必要があります。
        // RootへScreen固有の名前を強制しないため、現在は最後に追加されたChildを対象にします。
        root = child.get();
    }

    if (root == nullptr)
    {
        return false;
    }

    Scope<UIElement> detached = m_Context.GetRootElement().DetachChild(root);
    if (detached == nullptr)
    {
        return false;
    }

    screen.RestoreRootElement(std::move(detached));
    return true;
}

} // namespace Raven
