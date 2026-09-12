// Raven/Character/Debug/CharacterPositionDebugOverlayLayer.h
#pragma once

#include <imgui.h>

#include "Raven/Character/Debug/CharacterControllerDemoLayer.h"
#include "Raven/Core/Input.h"
#include "Raven/Core/KeyCodes.h"
#include "Raven/Math/MathVector.h"
#include "Raven/Renderer/Layer/Layer.h"

namespace Raven
{

// ============================================================================
// CharacterPositionDebugOverlayLayer
// ============================================================================
// CharacterControllerDemoLayerが保持する足元Root座標を常時表示する軽量Debug HUDです。
// Locomotion調整HUDとは責務を分離し、Fluidなど原点から離れた検証エリアへ移動するときでも
// 現在のWorld座標とDebug移動先を一目で比較できるようにします。
//
// Debug Teleport先は外部から注入し、このOverlay自身はFluid等の個別Domainを知りません。
// これによりCharacter -> Fluidの依存を作らず、別の遠隔検証エリアにも同じ仕組みを再利用できます。
class CharacterPositionDebugOverlayLayer final : public Layer
{
public:
    CharacterPositionDebugOverlayLayer(
        CharacterControllerDemoLayer& characterLayer,
        const math::Vec3& debugTeleportTarget)
        : m_CharacterLayer(&characterLayer)
        , m_DebugTeleportTarget(debugTeleportTarget)
    {
    }

    void OnDetach() override
    {
        m_CharacterLayer = nullptr;
    }

    void OnImGuiRender(float deltaTime) override
    {
        static_cast<void>(deltaTime);

        if (m_CharacterLayer == nullptr)
        {
            return;
        }

        // Fは押しっぱなしで毎Frame Teleportしないよう立ち上がりEdgeだけを採用します。
        // Button操作でも同じTeleport APIを通し、入力経路ごとに状態初期化処理が分岐しないようにします。
        const bool teleportKeyPressed = Input::IsKeyPressed(Key::F);
        const bool teleportRequested =
            teleportKeyPressed == true && m_WasTeleportKeyPressed == false;
        m_WasTeleportKeyPressed = teleportKeyPressed;

        if (teleportRequested == true)
        {
            m_CharacterLayer->TeleportCharacterForDebug(m_DebugTeleportTarget);
        }

        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        if (viewport == nullptr)
        {
            return;
        }

        // 初回表示時だけ右上へ配置します。
        // 以降はImGuiのWindow位置を固定しないことで、タイトルバーをドラッグして
        // Fluid HUDなど他のDebug UIと重ならない位置へ自由に移動できます。
        constexpr float Margin = 10.0f;
        const ImVec2 initialWindowPosition{
            viewport->WorkPos.x + viewport->WorkSize.x - Margin,
            viewport->WorkPos.y + Margin
        };

        ImGui::SetNextWindowPos(
            initialWindowPosition,
            ImGuiCond_FirstUseEver,
            ImVec2{ 1.0f, 0.0f });
        ImGui::SetNextWindowBgAlpha(0.78f);

        // ドラッグ用タイトルバーを残しつつResize/Collapse等は不要なので個別に無効化します。
        // NoDecoration / NoMoveを使うとWindow全体がドラッグ不能になるため使用しません。
        const ImGuiWindowFlags windowFlags =
            ImGuiWindowFlags_AlwaysAutoResize
            | ImGuiWindowFlags_NoResize
            | ImGuiWindowFlags_NoCollapse
            | ImGuiWindowFlags_NoSavedSettings
            | ImGuiWindowFlags_NoFocusOnAppearing
            | ImGuiWindowFlags_NoNav;

        if (ImGui::Begin("Character Position Debug", nullptr, windowFlags) == true)
        {
            const math::Vec3& position = m_CharacterLayer->GetCharacterWorldPosition();
            const math::Vec3 offset = m_DebugTeleportTarget - position;
            const float distance = offset.Length();

            ImGui::TextUnformatted("Character World Position");
            ImGui::Separator();
            ImGui::Text("X : %.2f", position.x);
            ImGui::Text("Y : %.2f", position.y);
            ImGui::Text("Z : %.2f", position.z);
            ImGui::Separator();
            ImGui::Text(
                "Debug Target : (%.1f, %.1f, %.1f)",
                m_DebugTeleportTarget.x,
                m_DebugTeleportTarget.y,
                m_DebugTeleportTarget.z);
            ImGui::Text("Distance     : %.2f m", distance);
            ImGui::TextUnformatted("F : Teleport to debug target");

            if (ImGui::Button("Teleport to Debug Target") == true)
            {
                m_CharacterLayer->TeleportCharacterForDebug(m_DebugTeleportTarget);
            }
        }
        ImGui::End();
    }

private:
    CharacterControllerDemoLayer* m_CharacterLayer = nullptr;
    math::Vec3 m_DebugTeleportTarget{};
    bool m_WasTeleportKeyPressed = false;
};

} // namespace Raven
