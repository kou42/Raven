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

    UIElement& GetRootElement();
    const UIElement& GetRootElement() const;

protected:
    // 派生ScreenはRoot以下へWidgetを構築します。
    UIElement* AddChild(Scope<UIElement> child)
    {
        return GetRootElement().AddChild(std::move(child));
    }

    virtual void OnEnter() {}
    virtual void OnExit() {}
    virtual void OnPause() {}
    virtual void OnResume() {}

private:
    friend class UINavigationManager;

    Scope<UIElement> ReleaseRootElement();
    void SetAttachedRoot(UIElement* root) { m_AttachedRoot = root; }
    UIElement* GetAttachedRoot() const { return m_AttachedRoot; }
    void RestoreRootElement(Scope<UIElement> root);

private:
    // Treeへ接続していない間はScreenがRootを所有します。
    Scope<UIElement> m_RootElement;
    // 接続中はUIContextのRoot Treeが所有するため、同一Elementを非所有で追跡します。
    UIElement* m_AttachedRoot = nullptr;
};

} // namespace Raven
