// Raven/Character/Debug/CharacterPositionDebugOverlayLayer.h
#pragma once

#include "Raven/Character/Debug/CharacterControllerDemoLayer.h"
#include "Raven/Core/Application.h"
#include "Raven/Core/Input.h"
#include "Raven/Core/KeyCodes.h"
#include "Raven/Math/MathVector.h"
#include "Raven/Renderer/Layer/Layer.h"
#include "Raven/Scene/Scene.h"
#include "Raven/UI/Widgets/UIButton.h"
#include "Raven/UI/Widgets/UILabel.h"
#include "Raven/UI/Widgets/UIWindow.h"

#include <algorithm>
#include <cstdio>
#include <string>

namespace Raven
{

// CharacterControllerDemoLayerの足元Root座標とDebug移動先を表示するRaven UI HUDです。
// Scene/Character LayerをWidgetへ保持せず、操作時と更新時にActive Sceneから再解決します。
class CharacterPositionDebugOverlayLayer final : public Layer
{
public:
    CharacterPositionDebugOverlayLayer(
        Application& application, const math::Vec3& debugTeleportTarget)
        : m_Application(&application)
        , m_DebugTeleportTarget(debugTeleportTarget)
    {
    }

    void OnAttach() override
    {
        if (m_Application == nullptr)
        {
            return;
        }

        auto window = CreateScope<UIWindow>();
        window->SetTitle("Character Position Debug");
        window->SetPosition(math::Vec2(780.0f, 10.0f));
        window->SetSize(math::Vec2(340.0f, 294.0f));
        window->SetPreferredSize(math::Vec2(340.0f, 294.0f));

        auto addLabel = [this, &window](float y, const std::string& text) -> UILabel*
        {
            auto label = CreateScope<UILabel>();
            label->SetFont(m_Application->GetRuntimeUIFont());
            label->SetText(text);
            label->SetPosition(math::Vec2(12.0f, y));
            label->SetSize(math::Vec2(310.0f, 28.0f));
            label->SetHitTestVisible(false);
            return static_cast<UILabel*>(window->AddChild(std::move(label)));
        };

        addLabel(34.0f, "Character World Position");
        m_PositionX = addLabel(68.0f, "X : --");
        m_PositionY = addLabel(96.0f, "Y : --");
        m_PositionZ = addLabel(124.0f, "Z : --");
        m_Target = addLabel(158.0f, "Debug Target : --");
        m_Distance = addLabel(186.0f, "Distance : --");
        addLabel(214.0f, "F : Teleport to debug target");

        auto button = CreateScope<UIButton>();
        button->SetPosition(math::Vec2(12.0f, 246.0f));
        button->SetSize(math::Vec2(310.0f, 32.0f));
        button->SetFocusable(true);
        button->SetOnClick([this]() { TeleportToDebugTarget(); });
        auto caption = CreateScope<UILabel>();
        caption->SetFont(m_Application->GetRuntimeUIFont());
        caption->SetText("Teleport to Debug Target");
        caption->SetPosition(math::Vec2(8.0f, 3.0f));
        caption->SetSize(math::Vec2(290.0f, 26.0f));
        caption->SetHitTestVisible(false);
        button->AddChild(std::move(caption));
        window->AddChild(std::move(button));

        m_Window = m_Application->GetUIContext().GetRootElement().AddChild(std::move(window));
    }

    void OnDetach() override
    {
        if (m_Application != nullptr && m_Window != nullptr)
        {
            m_Application->GetUIContext().GetRootElement().RemoveChild(m_Window);
        }
        m_Window = nullptr;
        m_PositionX = nullptr;
        m_PositionY = nullptr;
        m_PositionZ = nullptr;
        m_Target = nullptr;
        m_Distance = nullptr;
        m_Application = nullptr;
    }

    void OnUpdate(float deltaTime) override
    {
        static_cast<void>(deltaTime);
        if (m_Application == nullptr)
        {
            return;
        }

        // 描画フックから入力を切り離し、ImGuiが無効でもFキーの立ち上がりを処理します。
        const bool pressed = Input::IsKeyPressed(Key::F);
        const bool requested = pressed == true && m_WasTeleportKeyPressed == false;
        m_WasTeleportKeyPressed = pressed;
        if (requested == true)
        {
            TeleportToDebugTarget();
        }

        CharacterControllerDemoLayer* character = ResolveCharacterLayer();
        if (m_Window == nullptr)
        {
            return;
        }
        m_Window->SetVisible(character != nullptr);
        if (character == nullptr)
        {
            return;
        }

        const math::Vec3 position = character->GetCharacterWorldPosition();
        const float distance = (m_DebugTeleportTarget - position).Length();
        char buffer[160];
        std::snprintf(buffer, sizeof(buffer), "X : %.2f", position.x);
        m_PositionX->SetText(buffer);
        std::snprintf(buffer, sizeof(buffer), "Y : %.2f", position.y);
        m_PositionY->SetText(buffer);
        std::snprintf(buffer, sizeof(buffer), "Z : %.2f", position.z);
        m_PositionZ->SetText(buffer);
        std::snprintf(buffer, sizeof(buffer), "Debug Target : (%.1f, %.1f, %.1f)",
            m_DebugTeleportTarget.x, m_DebugTeleportTarget.y, m_DebugTeleportTarget.z);
        m_Target->SetText(buffer);
        std::snprintf(buffer, sizeof(buffer), "Distance : %.2f m", distance);
        m_Distance->SetText(buffer);
    }

private:
    void TeleportToDebugTarget()
    {
        // UI callback時にも再解決し、Scene交換後の古いCharacterを操作しません。
        CharacterControllerDemoLayer* character = ResolveCharacterLayer();
        if (character != nullptr)
        {
            character->TeleportCharacterForDebug(m_DebugTeleportTarget);
        }
    }

    CharacterControllerDemoLayer* ResolveCharacterLayer() const
    {
        if (m_Application == nullptr)
        {
            return nullptr;
        }
        Scene* scene = m_Application->GetScene();
        if (scene == nullptr)
        {
            return nullptr;
        }
        return scene->FindLayer<CharacterControllerDemoLayer>();
    }

    Application* m_Application = nullptr;
    math::Vec3 m_DebugTeleportTarget{};
    bool m_WasTeleportKeyPressed = false;
    UIElement* m_Window = nullptr;
    UILabel* m_PositionX = nullptr;
    UILabel* m_PositionY = nullptr;
    UILabel* m_PositionZ = nullptr;
    UILabel* m_Target = nullptr;
    UILabel* m_Distance = nullptr;
};

} // namespace Raven
