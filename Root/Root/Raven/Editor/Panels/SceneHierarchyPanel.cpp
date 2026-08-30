#include "Raven/Editor/Panels/SceneHierarchyPanel.h"

#include "Raven/Editor/Command/CreateEntityCommand.h"
#include "Raven/Editor/EditorCommandHistory.h"
#include "Raven/Scene/Components.h"
#include "Raven/Scene/Scene.h"

#include <imgui.h>

#include <memory>

namespace Raven
{

void SceneHierarchyPanel::OnImGuiRender(Scene* scene, Entity& selectedEntity)
{
    ImGui::Begin("Scene Hierarchy");

    if (scene == nullptr)
    {
        // Active Sceneが無い状態では既存選択も無効です。
        // Scene差し替え時に古いSceneのEntityをEditor選択として残さないことが重要です。
        selectedEntity = Entity{};
        ImGui::TextDisabled("No active scene.");
        ImGui::End();
        return;
    }

    // ========================================================================
    // Selection validation
    // ========================================================================
    // EntityはIndexだけでなくGenerationも持つため、破棄されたEntityを安全に検出できます。
    // Runtime側でEntityがDestroyされた場合でも、次frameで選択をクリアすることで
    // Inspector/Gizmoが無効なComponentへアクセスすることを防ぎます。
    if (selectedEntity && scene->IsEntityAlive(selectedEntity) == false)
    {
        selectedEntity = Entity{};
    }

    // ========================================================================
    // Entity creation
    // ========================================================================
    // CreateEntityCommand自身がScene APIを呼ぶため、通常の「実行 + 履歴登録」経路を利用します。
    // Panelが先にEntityを生成してからCommandへ渡す形にしないことで、実行責務をCommandへ集約します。
    if (ImGui::Button("Create Entity"))
    {
        auto command = std::make_unique<CreateEntityCommand>(scene, "Entity");
        CreateEntityCommand* executedCommand = command.get();

        if (ExecuteAndRecordEditorCommand(std::move(command)) == true)
        {
            // ExecuteAndRecord後のCommand所有権はHistoryへ移っています。
            // raw pointerはこの同期処理中に生成Handleを読むためだけに使い、Panelへ保持しません。
            selectedEntity = executedCommand->GetCreatedEntity();
        }
    }

    ImGui::Separator();

    // ========================================================================
    // Entity list
    // ========================================================================
    // 現在はCreateEntity()が全EntityへTagComponentを付ける契約なので、Tag Viewを
    // Scene全Entityの列挙経路として使います。Editor専用のEntity配列を別に持たないため、
    // Runtimeで生成/破棄されたEntityも次frameで自動的にHierarchyへ反映されます。
    for (auto [entity, tag] : scene->View<TagComponent>())
    {
        const bool isSelected = selectedEntity && selectedEntity == entity;

        // ImGui IDには表示名ではなくGeneration込みEntity値を使います。
        // 同名Entityが複数存在してもWidget IDが衝突せず、Index再利用時も旧Entityと区別できます。
        ImGui::PushID(static_cast<int>(entity.GetIndex()));

        if (ImGui::Selectable(tag.Tag.c_str(), isSelected))
        {
            selectedEntity = entity;
        }

        // Debug時に同名Entityを区別しやすいよう、hover中だけHandle情報を表示します。
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Index: %u\nGeneration: %u",
                entity.GetIndex(),
                entity.GetGeneration());
        }

        ImGui::PopID();
    }

    // Hierarchyの空白部分をクリックすると選択を解除します。
    // Panel上で明示的に「何も選択していない」状態へ戻せるため、Inspector/Gizmo側も
    // 選択なし状態を自然に扱えます。
    if (ImGui::IsWindowHovered()
        && ImGui::IsAnyItemHovered() == false
        && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        selectedEntity = Entity{};
    }

    ImGui::End();
}

} // namespace Raven
