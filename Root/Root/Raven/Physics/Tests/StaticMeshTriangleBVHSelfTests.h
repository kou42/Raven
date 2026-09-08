#pragma once

namespace Raven::ph::tests
{

// StaticMesh Triangle BVHの構築、Geometry Cache、World->Local Query変換、
// RayCast接続、Capsule BVH Narrow PhaseをCPU Geometryだけで検証します。
void RunStaticMeshTriangleBVHSelfTests();

} // namespace Raven::ph::tests
