#pragma once

#include "Raven/Physics/Debug/PhysicsDebugSettings.h"
#include "Raven/Physics/Debug/PhysicsFieldDebugSampler.h"
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
    const math::Vec2& position = math::Vec2(16.0f, 16.0f),
    const char* sdfStatus = "Snapshot: not published")
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
    if (normalLabel != nullptr)
    {
        normalLabel->SetText(formatValue("Normal Length", settings.ContactNormalLength, 2));
    }
    UILabel* radiusLabel = immediate.Text("point-radius-label",
        formatValue("Point Radius", settings.ContactPointRadius, 3), font);
    if (radiusLabel != nullptr)
    {
        radiusLabel->SetPreferredSize(math::Vec2(280.0f, 20.0f));
    }
    changed = immediate.SliderFloat("point-radius",
        &settings.ContactPointRadius, 0.0f, 0.2f, math::Vec2(280.0f, 24.0f)) || changed;
    if (radiusLabel != nullptr)
    {
        radiusLabel->SetText(formatValue("Point Radius", settings.ContactPointRadius, 3));
    }

    // EndContainerの失敗は呼び出し側のFrame不均衡を示します。
    if (immediate.EndContainer() == false)
    {
        return false;
    }

    // 既存 Panel の高さを保ち、場の調整項目を横に並べます。
    UIPanel* fieldPanel = immediate.BeginPanel("physics-field-debug",
        position + math::Vec2(310.0f, 0.0f), math::Vec2(300.0f, 532.0f));
    if (fieldPanel == nullptr)
    {
        return changed;
    }
    fieldPanel->SetSpacing(4.0f);
    fieldPanel->SetBackgroundColor(math::Vec4(0.07f, 0.10f, 0.16f, 0.94f));
    if (immediate.Button("field-controls",
            settings.ShowSDFControls ? "Vector Controls"
                : (settings.ShowTemperatureControls ? "SDF Controls" : "Temperature Controls"),
            font, math::Vec2(280.0f, 28.0f)))
    {
        const bool nextSDF = settings.ShowTemperatureControls && settings.ShowSDFControls == false;
        settings.ShowTemperatureControls = settings.ShowTemperatureControls == false && settings.ShowSDFControls == false;
        settings.ShowSDFControls = nextSDF;
        changed = true;
    }
    auto addFieldSlider = [&](const char* id, const char* labelId, const char* caption,
        float& value, float minimum, float maximum, int precision)
    {
        UILabel* label = immediate.Text(labelId, formatValue(caption, value, precision), font);
        changed = immediate.SliderFloat(id, &value, minimum, maximum, math::Vec2(280.0f, 24.0f)) || changed;
        if (label != nullptr)
        {
            label->SetPreferredSize(math::Vec2(280.0f, 20.0f));
            label->SetText(formatValue(caption, value, precision));
        }
    };
    auto addSliceControls = [&](PhysicsFieldSlicePlane& plane, float& coordinate, bool sdf)
    {
        const char* planeLabel = plane == PhysicsFieldSlicePlane::XY ? "Slice XY"
            : (plane == PhysicsFieldSlicePlane::YZ ? "Slice YZ" : "Slice XZ");
        if (immediate.Button(sdf ? "sdf-plane" : "thermal-plane", planeLabel, font, math::Vec2(280.0f, 28.0f)))
        {
            plane = plane == PhysicsFieldSlicePlane::XZ ? PhysicsFieldSlicePlane::XY
                : (plane == PhysicsFieldSlicePlane::XY ? PhysicsFieldSlicePlane::YZ : PhysicsFieldSlicePlane::XZ);
            changed = true;
        }
        const char* coordinateLabel = plane == PhysicsFieldSlicePlane::XY ? "Slice Z (m)"
            : (plane == PhysicsFieldSlicePlane::YZ ? "Slice X (m)" : "Slice Y (m)");
        addFieldSlider(sdf ? "sdf-position" : "thermal-position",
            sdf ? "sdf-position-label" : "thermal-position-label", coordinateLabel, coordinate, -10.0f, 10.0f, 2);
    };
    if (settings.ShowSDFControls)
    {
        const math::Vec2 controlSize{ 280.0f, 28.0f };
        changed = immediate.Checkbox("sdf-dense-signed", "Dense Signed SDF",
            &settings.ShowDenseSignedSDF, font, controlSize) || changed;
        changed = immediate.Checkbox("sdf-dense-unsigned", "Dense Unsigned SDF",
            &settings.ShowDenseUnsignedSDF, font, controlSize) || changed;
        changed = immediate.Checkbox("sdf-sparse", "Sparse SDF",
            &settings.ShowSparseSDF, font, controlSize) || changed;
        addSliceControls(settings.SDFSlicePlane, settings.SDFSliceCoordinate, true);
        addFieldSlider("sdf-range", "sdf-range-label", "Distance Range (m)",
            settings.SDFDistanceRange, 0.05f, 2.0f, 2);
        immediate.Text("sdf-status", sdfStatus, font);
        immediate.Text("sdf-signed-legend", "Signed: blue inside, red outside", font);
        immediate.Text("sdf-contour-legend", "Black/white line: zero distance", font);
        immediate.Text("sdf-unsigned-legend", "Unsigned: cyan near, yellow far", font);
        immediate.Text("sdf-grid-legend", "Gray: outside grid", font);
        immediate.Text("sdf-band-legend", "Orange: outside band", font);
        immediate.Text("sdf-support-legend", "Purple: missing support", font);
        return immediate.EndContainer() == true && changed;
    }
    if (settings.ShowTemperatureControls)
    {
        const math::Vec2 controlSize{ 280.0f, 28.0f };
        changed = immediate.Checkbox("thermal-uniform", "Uniform Temperature",
            &settings.ShowUniformTemperatureField, font, controlSize) || changed;
        changed = immediate.Checkbox("thermal-gradient", "Gradient Temperature",
            &settings.ShowGradientTemperatureField, font, controlSize) || changed;
        changed = immediate.Checkbox("thermal-sphere", "Sphere Temperature Region",
            &settings.ShowSphericalTemperatureField, font, controlSize) || changed;
        changed = immediate.Checkbox("thermal-box", "Box Temperature Region",
            &settings.ShowBoxTemperatureField, font, controlSize) || changed;
        changed = immediate.Checkbox("thermal-influence", "Show Influence (0-1)",
            &settings.ShowTemperatureInfluence, font, controlSize) || changed;
        changed = immediate.Checkbox("thermal-bounds", "Region Bounds",
            &settings.ShowTemperatureBounds, font, controlSize) || changed;
        addSliceControls(settings.TemperatureSlicePlane, settings.TemperatureSliceCoordinate, false);
        if (settings.ShowTemperatureInfluence)
        {
            immediate.Text("thermal-legend", "Purple: low   Green: high", font);
        }
        else
        {
            addFieldSlider("thermal-min", "thermal-min-label", "Blue Minimum (K)",
                settings.TemperatureMinimum, 0.0f, 499.0f, 0);
            addFieldSlider("thermal-max", "thermal-max-label", "Red Maximum (K)",
                settings.TemperatureMaximum, settings.TemperatureMinimum + 1.0f, 1000.0f, 0);
        }
        return immediate.EndContainer() == true && changed;
    }

    changed = immediate.Checkbox("gravity", "Uniform Gravity (m/s^2)",
        &settings.ShowVectorFields, font, math::Vec2(280.0f, 28.0f)) || changed;
    changed = immediate.Checkbox("point-gravity", "Point Gravity (m/s^2)",
        &settings.ShowPointGravityFields, font, math::Vec2(280.0f, 28.0f)) || changed;
    changed = immediate.Checkbox("electric", "Electric Fields (N/C)",
        &settings.ShowElectricFields, font, math::Vec2(280.0f, 28.0f)) || changed;
    changed = immediate.Checkbox("point-charge", "Point Charge (N/C)",
        &settings.ShowPointChargeFields, font, math::Vec2(280.0f, 28.0f)) || changed;
    changed = immediate.Checkbox("magnetic", "Magnetic Fields (T)",
        &settings.ShowMagneticFields, font, math::Vec2(280.0f, 28.0f)) || changed;
    changed = immediate.Checkbox("dipole", "Magnetic Dipole (T)",
        &settings.ShowDipoleFields, font, math::Vec2(280.0f, 28.0f)) || changed;
    changed = immediate.Checkbox("field-bounds", "Grid Bounds",
        &settings.ShowFieldBounds, font, math::Vec2(280.0f, 28.0f)) || changed;
    addFieldSlider("grid-points", "grid-points-label", "Points per Axis", settings.FieldGridPointsPerAxis,
        1.0f, static_cast<float>(PhysicsFieldDebugGrid::MaxPointsPerAxis), 0);
    addFieldSlider("grid-spacing", "grid-spacing-label", "Grid Spacing (m)", settings.FieldGridSpacing,
        PhysicsFieldDebugGrid::MinSpacing, PhysicsFieldDebugGrid::MaxSpacing, 2);
    addFieldSlider("arrow-scale", "arrow-scale-label", "Gravity Arrow Scale", settings.GravityArrowScale,
        0.1f, 4.0f, 2);
    addFieldSlider("electric-scale", "electric-scale-label", "Electric Arrow Scale", settings.ElectricArrowScale,
        0.1f, 4.0f, 2);
    addFieldSlider("magnetic-scale", "magnetic-scale-label", "Magnetic Arrow Scale", settings.MagneticArrowScale,
        0.1f, 4.0f, 2);
    return immediate.EndContainer() == true && changed;
}

} // namespace Raven::ph
