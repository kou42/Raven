#pragma once

#include "Raven/Physics/Debug/PhysicsFieldDebugSampler.h"

namespace Raven::ph
{

struct PhysicsDebugSettings
{
    bool ShowSolverStatistics = true;
    bool ShowContactPoints = false;
    bool ShowContactNormals = false;

    // B: Broad Phase / query用のtight AABB。
    bool ShowAABB = false;

    // O: Narrow Phaseが実際に使用する回転Box(OBB)を表示します。
    // AABBと同時表示すると、Broad Phase候補領域と実Collider形状の差を確認できます。
    bool ShowOBB = false;

    bool ShowFatAABB = false;
    bool ShowDynamicAABBTree = false;
    bool ShowBroadPhasePairs = false;

    float ContactNormalLength = 0.5f;
    float ContactPointRadius = 0.03f;

    bool ShowVectorFields = false;
    bool ShowPointGravityFields = false;
    bool ShowElectricFields = false;
    bool ShowPointChargeFields = false;
    bool ShowMagneticFields = false;
    bool ShowDipoleFields = false;
    bool ShowFieldBounds = true;
    float FieldGridPointsPerAxis = 7.0f;
    float FieldGridSpacing = 1.0f;
    float GravityArrowScale = 1.0f;
    float ElectricArrowScale = 1.0f;
    float MagneticArrowScale = 1.0f;

    static constexpr float DefaultTemperatureMinimum = 260.0f;
    static constexpr float DefaultTemperatureMaximum = 360.0f;
    bool ShowTemperatureControls = false;
    bool ShowUniformTemperatureField = false;
    bool ShowGradientTemperatureField = false;
    bool ShowSphericalTemperatureField = false;
    bool ShowBoxTemperatureField = false;
    bool ShowTemperatureInfluence = false;
    bool ShowTemperatureBounds = true;
    PhysicsFieldSlicePlane TemperatureSlicePlane = PhysicsFieldSlicePlane::XZ;
    float TemperatureSliceCoordinate = 0.0f;
    float TemperatureMinimum = DefaultTemperatureMinimum;
    float TemperatureMaximum = DefaultTemperatureMaximum;

    bool HasVisibleTemperatureField() const
    {
        return ShowUniformTemperatureField || ShowGradientTemperatureField
            || ShowSphericalTemperatureField || ShowBoxTemperatureField;
    }

    bool HasVisibleVectorField() const
    {
        return ShowVectorFields || ShowPointGravityFields || ShowElectricFields
            || ShowPointChargeFields || ShowMagneticFields || ShowDipoleFields;
    }
};

} // namespace Raven::ph
