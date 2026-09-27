#pragma once

#include "Raven/Core/Base.h"
#include "Raven/UI/Core/UIElement.h"

namespace Raven
{

class UINavigationManager;

// Scene上に重ねるUI画面の基底クラスです。
// Sceneの生成・破棄とは独立したLifetimeを持ち、Pause / Settings / Dialogのような
// 画面内NavigationをSceneManagerへ持ち込まないための境界として利用します。
class UIScreen
{
public:
    UIScreen();
    virtual ~UIScreen() = default;

    UIScreen(const UIScreen&) = delete;
    UIScreen& operator=(const UIScreen&) = delete;
    UIScreen(UIScreen&&) = delete;
    UIScreen& operator=(UIScreen&&) = delete;

    UIElement& GetRootElement() { return *m_RootElement; }
    const UIElement& GetRootElement() const { return *m_RootElement; }

protected:
    // 派生ScreenはRoot以下へWidgetを構築します。
    // Root自体の所有権はNavigationManagerとの着脱時もUIScreenへ戻るため、
    // ScreenのLifetimeとRetained UI TreeのLifetimeを一致させられます。
    UIElement* AddChild(Scope<UIElement> child)
    {
        return m_RootElement->AddChild(std::move(child));
    }

    virtual void OnEnter() {}
    virtual void OnExit() {}
    virtual void OnPause() {}
    virtual void OnResume() {}

private:
    friend class UINavigationManager;

    Scope<UIElement> ReleaseRootElement();
    void RestoreRootElement(Scope<UIElement> root);

private:
    Scope<UIElement> m_RootElement;
};

} // namespace Raven
