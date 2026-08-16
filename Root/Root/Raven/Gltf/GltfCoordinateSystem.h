// Raven/Gltf/GltfCoordinateSystem.h
#pragma once

#include "Raven/Math/MathMatrix.h"

namespace Raven
{
namespace Gltf
{

// ============================================================================
// glTF -> Raven 座標系契約
// ============================================================================
// glTF 2.0ではAsset全体の基準座標系が仕様で固定されています。
// Up軸は +Y であり、Mesh / Node / Skin Jointはすべて同じ基準座標系に属します。
//
// 重要:
// ここでいう「+Y up」は座標系の基底規約です。Human Meshそのものが必ず+Y方向へ
// 直立して格納されることまでは保証しません。ExporterによってはAuthoring Tool上の姿勢を
// Geometry / Joint Bind Poseへ焼き込み、glTF Scene Space上では人物が横向きに格納されることがあります。
//
// したがって、次の2つは別責務として扱います。
// - glTF Scene Space -> Raven World Space : このファイルで定義する座標系変換
// - Humanoid Bind Pose -> Ravenの直立方向 : Human Debug側でSkeletonの意味情報から解決
//
// 後者をMesh AABBの長軸から推測すると、T-Poseの腕幅などに結果が左右されるため禁止します。
// Humanの直立方向が必要な場合は、Hips -> HeadのようなSkeleton Bind Pose上の意味的な方向を使います。
//
// Ravenの現在のScene座標系も +Y up として扱っているため、glTF Scene Spaceから
// Raven World Spaceへの基底変換はIdentityです。ただし「たまたまIdentityだから何もしない」
// のではなく、この関数をScene配置境界で明示的に通すことで座標系契約を1か所へ集約します。
inline math::Mat4 BuildGltfToRavenWorldTransform()
{
    return math::Mat4::Identity();
}

inline const char* GetGltfCoordinateSystemDescription()
{
    return "+Y up (glTF 2.0 Node/Skin coordinate system)";
}

} // namespace Gltf
} // namespace Raven
