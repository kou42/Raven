#pragma once

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
};

} // namespace Raven::ph
