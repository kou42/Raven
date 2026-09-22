#pragma once

#include "Raven/Physics/Debug/PhysicsDebugSettings.h"
#include "Raven/UI/Immediate/UIImmediateContext.h"

#include <iomanip>
#include <sstream>

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
        "physics-debug", position, math::Vec2(300.0f, 430.0f));
    if (panel == nullptr)
    {
        return false;
    }

    // Checkboxと数値Label/Sliderを収め、再利用時も宣言順で配置します。
    panel->SetSpacing(4.0f);
    panel->SetBackgroundColor(math::Vec4(0.07f, 0.10f, 0.16f, 0.94f));
    UILabel* title = immediate.Text("title", "Physics Debug", font);
    if (title != nullptr)
    {
        title->SetPreferredSize(math::Vec2(280.0f, 24.0f));
    }

    bool changed = false;
    changed = immediate.Checkbox("statistics", "Solver Statistics",
        &settings.ShowSolverStatistics, font, math::Vec2(280.0f, 28.0f)) || changed;
    changed = immediate.Checkbox("contacts", "Contact Points",
        &settings.ShowContactPoints, font, math::Vec2(280.0f, 28.0f)) || changed;
    changed = immediate.Checkbox("normals", "Contact Normals",
        &settings.ShowContactNormals, font, math::Vec2(280.0f, 28.0f)) || changed;
    changed = immediate.Checkbox("aabb", "AABB",
        &settings.ShowAABB, font, math::Vec2(280.0f, 28.0f)) || changed;
    changed = immediate.Checkbox("obb", "OBB",
        &settings.ShowOBB, font, math::Vec2(280.0f, 28.0f)) || changed;
    changed = immediate.Checkbox("fat-aabb", "Fat AABB",
        &settings.ShowFatAABB, font, math::Vec2(280.0f, 28.0f)) || changed;
    changed = immediate.Checkbox("tree", "Dynamic AABB Tree",
        &settings.ShowDynamicAABBTree, font, math::Vec2(280.0f, 28.0f)) || changed;
    changed = immediate.Checkbox("pairs", "Broad Phase Pairs",
        &settings.ShowBroadPhasePairs, font, math::Vec2(280.0f, 28.0f)) || changed;
    // Settingsを数値表示の正規データとし、入力反映後の値を次Frameで描画します。
    auto formatValue = [](const char* caption, float value, int precision)
    {
        std::ostringstream stream;
        stream << caption << ": " << std::fixed << std::setprecision(precision) << value;
        return stream.str();
    };
    UILabel* normalLabel = immediate.Text("normal-length-label",
        formatValue("Normal Length", settings.ContactNormalLength, 2), font);
    if (normalLabel != nullptr)
    {
        normalLabel->SetPreferredSize(math::Vec2(280.0f, 20.0f));
    }
    changed = immediate.SliderFloat("normal-length",
        &settings.ContactNormalLength, 0.0f, 2.0f, math::Vec2(280.0f, 24.0f)) || changed;
    UILabel* radiusLabel = immediate.Text("point-radius-label",
        formatValue("Point Radius", settings.ContactPointRadius, 3), font);
    if (radiusLabel != nullptr)
    {
        radiusLabel->SetPreferredSize(math::Vec2(280.0f, 20.0f));
    }
    changed = immediate.SliderFloat("point-radius",
        &settings.ContactPointRadius, 0.0f, 0.2f, math::Vec2(280.0f, 24.0f)) || changed;

    // EndContainerの失敗は呼び出し側のFrame不均衡を示します。
    return immediate.EndContainer() == true && changed;
}

} // namespace Raven::ph
