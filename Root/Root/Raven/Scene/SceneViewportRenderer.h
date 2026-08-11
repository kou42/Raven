#pragma once

#include "Raven/Renderer/Camera/Camera.h"

namespace Raven
{

// ============================================================================
// SceneViewportRenderer
// ============================================================================
// Runtime Sceneを「Scene自身が通常使用するCamera」とは別のCameraで描画するための
// 小さな描画インターフェースです。
//
// Scene ViewではEditorCameraを使いたい一方、Game Viewでは今後SceneCameraを利用します。
// 描画入口が具体的なCamera型を知らないようCamera基底を受け取ることで、Renderer側へ
// EditorCamera / SceneCameraの違いを持ち込みません。
//
// 現段階ではScene基底クラス全体の責務を大きく変更しないため独立インターフェースとしています。
// 今後Scene Rendererを本格的に分離する段階では、この責務をSceneRendererへ統合できます。
class SceneViewportRenderer
{
public:
    virtual ~SceneViewportRenderer() = default;

    // 指定CameraのView / Projectionを一時的な描画Cameraとして使用してSceneを描画します。
    // Runtime Cameraの永続状態を変更しないことが実装側の契約です。
    virtual void RenderWithCamera(const Camera& camera) = 0;

    // ========================================================================
    // 移行期間用Adapter
    // ========================================================================
    // EditorLayer側をCamera参照へ切り替えるまで、既存のView/Projection呼び出しも受け付けます。
    // ここで一時Cameraへ包んだ後は必ず新しいRenderWithCamera(const Camera&)へ集約されるため、
    // Scene実装側に旧インターフェースを残す必要はありません。
    //
    // このAdapterはCamera抽象化の途中段階だけで利用し、EditorLayerからEditorCameraを直接渡す
    // 対応が完了した時点で削除できます。
    void RenderWithCamera(
        const math::Mat4& view,
        const math::Mat4& projection)
    {
        class MatrixCamera final : public Camera
        {
        public:
            MatrixCamera(const math::Mat4& viewMatrix, const math::Mat4& projectionMatrix)
                : m_View(viewMatrix)
                , m_Projection(projectionMatrix)
            {
            }

            const math::Mat4& GetViewMatrix() const override { return m_View; }
            const math::Mat4& GetProjectionMatrix() const override { return m_Projection; }

        private:
            const math::Mat4& m_View;
            const math::Mat4& m_Projection;
        };

        const MatrixCamera camera(view, projection);
        RenderWithCamera(camera);
    }
};

} // namespace Raven
