#pragma once

#include "Raven/Math/MathMatrix.h"

namespace Raven
{

// ============================================================================
// SceneViewportRenderer
// ============================================================================
// Runtime Sceneを「Scene自身が通常使用するCamera」とは別のView/Projectionで描画するための
// 小さな描画インターフェースです。
//
// Scene ViewではEditorCameraを使いたい一方、Game ViewではRuntime Cameraを維持する必要があります。
// SceneのUpdate状態やEntityを複製せず、同じSceneを別Cameraから再描画できるようにすることが目的です。
//
// 現段階ではScene基底クラス全体の責務を大きく変更しないため独立インターフェースとしています。
// 今後Scene Rendererを本格的に分離する段階では、この責務をSceneRendererへ統合できます。
class SceneViewportRenderer
{
public:
    virtual ~SceneViewportRenderer() = default;

    // 指定されたView/Projectionを一時的な描画Cameraとして使用してSceneを描画します。
    // Runtime Cameraの永続状態を変更しないことが実装側の契約です。
    virtual void RenderWithCamera(
        const math::Mat4& view,
        const math::Mat4& projection) = 0;
};

} // namespace Raven
