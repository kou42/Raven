#pragma once

#include "Raven/Core/Base.h"

namespace Raven
{

class Mesh;

// ============================================================================
// PrimitiveMeshFactory
// ============================================================================
// Scene側に頂点生成処理を置かず、Renderer側で再利用可能な基本形状を生成します。
//
// すべてのPrimitiveは原点中心・基準サイズ1.0で生成します。
// - Cube   : 各軸 -0.5 ～ +0.5
// - Sphere : 半径 0.5
//
// この規約により、SceneではTransform::Scaleだけで見た目の大きさを指定でき、
// ColliderのHalfExtents / Radiusとの対応も明確になります。
class PrimitiveMeshFactory
{
public:
    static Ref<Mesh> CreateCube();
    static Ref<Mesh> CreateSphere(int stacks = 24, int slices = 48);
};

} // namespace Raven
