#pragma once

namespace Raven::ph::tests
{

// StaticMesh Triangle BVHの構築、壊れたIndex除外、AABB/Ray候補Queryを
// RendererやPhysicsWorldを起動せずCPU Geometryだけで検証します。
void RunStaticMeshTriangleBVHSelfTests();

} // namespace Raven::ph::tests
