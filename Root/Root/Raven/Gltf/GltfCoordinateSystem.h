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
// Blender等のAuthoring Tool側がZ-upであっても、glTFへExportされた時点で必要な変換は
// Node階層へ符号化されます。そのためImporter / Debug表示側がMesh AABBを見て
// 「このAssetはZ-upらしい」と推測して追加回転を掛けてはいけません。
//
// Ravenの現在のScene座標系も +Y up として扱っているため、glTF Scene Spaceから
// Raven World Spaceへの基底変換はIdentityです。ただし「たまたまIdentityだから何もしない」
// のではなく、この関数をScene配置境界で明示的に通すことで座標系契約を1か所へ集約します。
// 将来Raven側の基底規約を変更する場合も、GeometryやSkeletonへ個別補正を散らさず、
// この変換だけを更新できる構造にします。
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
