#include "Raven/UI/Svg/Debug/UISvgDemoLayer.h"

#include "Raven/Core/Application.h"
#include "Raven/UI/Core/UIContext.h"
#include "Raven/UI/Svg/UISvg.h"

#include <iostream>
#include <string>
#include <utility>

namespace Raven
{

UISvgDemoLayer::UISvgDemoLayer(Application& application)
    : m_Application(application)
{
}

void UISvgDemoLayer::OnAttach()
{
    auto svg = CreateScope<UISvg>();
    svg->SetPosition(math::Vec2(420.0f, 48.0f));

    std::string error;
    if (svg->LoadFromFile("Raven/Assets/Svg/AnimatedRect.svg", &error) == false)
    {
        std::cout << "[Raven SVG] Failed to load demo SVG: " << error << std::endl;
        return;
    }

    UIElement* attached = m_Application.GetUIContext().GetRootElement().AddChild(std::move(svg));
    if (attached == nullptr)
    {
        std::cout << "[Raven SVG] Failed to attach demo SVG to UI tree." << std::endl;
        return;
    }

    m_Svg = static_cast<UISvg*>(attached);
    m_Svg->Play();
}

void UISvgDemoLayer::OnDetach()
{
    if (m_Svg == nullptr)
    {
        return;
    }

    // Root Elementが所有するScopeをRemoveChild()で破棄します。
    // raw pointerは非所有参照なので、削除後は必ずnullptrへ戻して以降のUpdateから触れないようにします。
    m_Application.GetUIContext().GetRootElement().RemoveChild(m_Svg);
    m_Svg = nullptr;
}

void UISvgDemoLayer::OnUpdate(float dt)
{
    if (m_Svg == nullptr)
    {
        return;
    }

    m_Svg->Update(dt);
}

} // namespace Raven
