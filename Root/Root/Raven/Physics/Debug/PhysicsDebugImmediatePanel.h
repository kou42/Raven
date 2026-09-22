#pragma once

#include "Raven/Physics/Debug/PhysicsDebugSettings.h"
#include "Raven/UI/Immediate/UIImmediateContext.h"

namespace Raven::ph
{

// PhysicsDebugRenderer::GetSettings()をImmediate UIで編集するための宣言Helperです。
// UIContextのBeginFrameより前に、呼び出し側が開始したImmediate Frame内で呼んでください。
// 既存のKeyboard Toggle / Overlay描画経路は変更せず、同じSettingsを共有します。
inline bool DrawPhysicsDebugImmediatePanel(
    UIImmediateContext& immediate, PhysicsDebugSettings& settings,
    const Ref<UIFontAtlas>& font,
    const math::Vec2& position = math::Vec2(16.0f, 16.0f))
{
    UIPanel* panel = immediate.BeginPanel(
        "physics-debug", position, math::Vec2(260.0f, 410.0f));
    if (panel == nullptr)
    {
        return false;
    }

    immediate.Text("title", "Physics Debug", font);

    bool changed = false;
    changed = immediate.Checkbox("statistics", "Solver Statistics",
        &settings.ShowSolverStatistics, font) || changed;
    changed = immediate.Checkbox("contacts", "Contact Points",
        &settings.ShowContactPoints, font) || changed;
    changed = immediate.Checkbox("normals", "Contact Normals",
        &settings.ShowContactNormals, font) || changed;
    changed = immediate.Checkbox("aabb", "AABB",
        &settings.ShowAABB, font) || changed;
    changed = immediate.Checkbox("obb", "OBB",
        &settings.ShowOBB, font) || changed;
    changed = immediate.Checkbox("fat-aabb", "Fat AABB",
        &settings.ShowFatAABB, font) || changed;
    changed = immediate.Checkbox("tree", "Dynamic AABB Tree",
        &settings.ShowDynamicAABBTree, font) || changed;
    changed = immediate.Checkbox("pairs", "Broad Phase Pairs",
        &settings.ShowBroadPhasePairs, font) || changed;
    changed = immediate.SliderFloat("normal-length",
        &settings.ContactNormalLength, 0.0f, 2.0f) || changed;
    changed = immediate.SliderFloat("point-radius",
        &settings.ContactPointRadius, 0.0f, 0.2f) || changed;

    // EndContainerの失敗は呼び出し側のFrame不均衡を示します。
    return immediate.EndContainer() == true && changed;
}

} // namespace Raven::ph
