#include "Raven/Editor/Tests/InspectorSnapshotSelfTests.h"

#include "Raven/Editor/Panels/InspectorRavenPanel.h"
#include "Raven/Editor/Panels/InspectorSnapshot.h"
#include "Raven/Scene/Scene.h"
#include "Raven/UI/Core/UIContext.h"
#include "Raven/UI/Widgets/UIComboBox.h"
#include "Raven/UI/Widgets/UIInputNumber.h"

#include <cassert>
#include <vector>

namespace Raven::tests
{
namespace
{

template<class TWidget>
void CollectWidgets(UIElement& element, std::vector<TWidget*>& widgets)
{
    if (auto* widget = dynamic_cast<TWidget*>(&element))
    {
        widgets.push_back(widget);
    }
    for (const auto& child : element.GetChildren())
    {
        if (child != nullptr)
        {
            CollectWidgets(*child, widgets);
        }
    }
}

} // namespace

void RunInspectorSnapshotSelfTests()
{
    const InspectorSnapshot empty = CaptureInspectorSnapshot(Entity{});
    assert(empty.HasSelection == false);
    assert(empty.SelectionValid == false);

    Scene scene;
    Entity entity = scene.CreateEntity("Inspector Target");
    entity.GetComponent<TransformComponent>().Position = math::Vec3(2.0f, 4.0f, 6.0f);

    RigidBodyComponent rigidBody;
    rigidBody.SetBodyType(BodyType::Kinematic);
    rigidBody.LinearVelocity = math::Vec3(1.0f, 3.0f, 5.0f);
    entity.AddComponent<RigidBodyComponent>(rigidBody);

    const InspectorSnapshot captured = CaptureInspectorSnapshot(entity);
    assert(captured.HasSelection == true);
    assert(captured.SelectionValid == true);
    assert(captured.Handle == entity.GetHandle());
    assert(captured.Tag.has_value() == true);
    assert(*captured.Tag == "Inspector Target");
    assert(captured.Transform.has_value() == true);
    assert(captured.Transform->Position.y == 4.0f);
    assert(captured.Camera.has_value() == false);
    assert(captured.RigidBody.has_value() == true);
    assert(captured.RigidBody->Type == BodyType::Kinematic);
    assert(captured.RigidBody->LinearVelocity.z == 5.0f);
    assert(captured.Collider.has_value() == false);

    entity.AddComponent<CameraComponent>();
    entity.AddComponent<ColliderComponent>();
    const InspectorSnapshot editable = CaptureInspectorSnapshot(entity);

    UIContext context;
    InspectorRavenPanel panel;
    int rigidBodyPreviewCount = 0;
    int rigidBodyCommitCount = 0;
    int rigidBodyChangeCount = 0;
    int colliderPreviewCount = 0;
    int colliderCommitCount = 0;
    int colliderChangeCount = 0;
    panel.SetRigidBodyPreviewRequestedHandler(
        [&rigidBodyPreviewCount](EntityHandle, InspectorRigidBodyField,
            const InspectorRigidBodySnapshot&)
        {
            ++rigidBodyPreviewCount;
            return true;
        });
    panel.SetRigidBodyCommitRequestedHandler(
        [&rigidBodyCommitCount](EntityHandle, InspectorRigidBodyField,
            const InspectorRigidBodySnapshot&)
        {
            ++rigidBodyCommitCount;
            return true;
        });
    panel.SetRigidBodyChangeRequestedHandler(
        [&rigidBodyChangeCount](EntityHandle, InspectorRigidBodyField,
            const InspectorRigidBodySnapshot&)
        {
            ++rigidBodyChangeCount;
            return true;
        });
    panel.SetColliderPreviewRequestedHandler(
        [&colliderPreviewCount](EntityHandle, InspectorColliderField,
            const InspectorColliderSnapshot&)
        {
            ++colliderPreviewCount;
            return true;
        });
    panel.SetColliderCommitRequestedHandler(
        [&colliderCommitCount](EntityHandle, InspectorColliderField,
            const InspectorColliderSnapshot&)
        {
            ++colliderCommitCount;
            return true;
        });
    panel.SetColliderChangeRequestedHandler(
        [&colliderChangeCount](EntityHandle, InspectorColliderField,
            const InspectorColliderSnapshot&)
        {
            ++colliderChangeCount;
            return true;
        });
    assert(panel.Attach(context, nullptr) == true);
    panel.Update(editable);

    // Snapshot同期でComboBoxのSelectionChangedを編集Actionとして誤通知しません。
    assert(rigidBodyChangeCount == 0);
    assert(colliderChangeCount == 0);

    std::vector<UIComboBox*> comboBoxes;
    std::vector<UIInputNumber*> numberInputs;
    CollectWidgets(context.GetRootElement(), comboBoxes);
    CollectWidgets(context.GetRootElement(), numberInputs);
    assert(comboBoxes.size() == 2u);
    assert(numberInputs.size() == 36u);

    assert(comboBoxes[0u]->SetSelectedIndex(
        static_cast<std::size_t>(BodyType::Static)) == true);
    assert(rigidBodyChangeCount == 1);
    assert(comboBoxes[1u]->SetSelectedIndex(
        static_cast<std::size_t>(ColliderType::Sphere)) == true);
    assert(colliderChangeCount == 1);

    // InputNumberのValueChangedはPreview、Commitは編集終了時の1回だけ通知します。
    numberInputs[12u]->Increment();
    numberInputs[12u]->Commit();
    assert(rigidBodyPreviewCount == 1);
    assert(rigidBodyCommitCount == 1);
    numberInputs[21u]->Increment();
    numberInputs[21u]->Commit();
    assert(colliderPreviewCount == 1);
    assert(colliderCommitCount == 1);
    panel.Detach();

    scene.DestroyEntity(entity);
    const InspectorSnapshot destroyed = CaptureInspectorSnapshot(entity);
    assert(destroyed.HasSelection == true);
    assert(destroyed.SelectionValid == false);
    assert(static_cast<bool>(destroyed.Handle) == false);
    assert(destroyed.Tag.has_value() == false);
}

} // namespace Raven::tests
