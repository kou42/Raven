#pragma once

#include "Raven/Core/Base.h"
#include "Raven/Math/MathMatrix.h"

#include <cstdint>

namespace Raven
{

class Camera;
class Material;
class Mesh;
class RendererAPI;
class Shader;
class VertexArray;

// ============================================================================
// RendererStatistics
// ============================================================================
// 1 frame中にRendererが実際に発行した描画量を保持します。
// EditorのStatisticsPanelだけでなく、将来Profilerや自動テストからも参照できるよう
// ImGui依存をRendererへ持ち込まず、純粋なEngine統計データとして定義します。
struct RendererStatistics
{
    uint32_t DrawCalls = 0;
    uint32_t IndexCount = 0;
    uint32_t TriangleCount = 0;

    void Reset()
    {
        DrawCalls = 0;
        IndexCount = 0;
        TriangleCount = 0;
    }
};

// ============================================================================
// RendererCameraContext
// ============================================================================
// 現在のScene描画で利用しているCamera行列をRenderer側へ保持する小さなContextです。
// Cameraオブジェクトへの参照を保持せず行列をコピーするため、EditorCamera/SceneCameraの
// 具体型や寿命にRendererが依存しません。
struct RendererCameraContext
{
    math::Mat4 View = math::Mat4::Identity();
    math::Mat4 Projection = math::Mat4::Identity();
    bool Valid = false;
};

class Renderer
{
public:
    static void Init();
    static void Shutdown();

    static void BeginFrame();

    // Cameraを伴う通常の3D Scene描画入口です。
    // BeginSceneからEndSceneまで、このCameraが通常描画とDebug Passの共通Contextになります。
    static void BeginScene(const Camera& camera);

    // Cameraを必要としない旧Sandbox等の簡易描画用入口です。
    // Camera Contextは無効化されるため、Camera依存のDebug描画は実行されません。
    static void BeginScene();
    static void EndScene();

    static const RendererCameraContext& GetCameraContext();

    static void Submit(const Ref<Shader>& shader, const Ref<VertexArray>& vertexArray);
    static void DrawIndexed(const Ref<VertexArray>& vertexArray);
    static void Draw(const Ref<Mesh>& mesh, const Ref<Material>& material, const math::Mat4& transform);

    static RendererAPI& GetAPI();

    static const RendererStatistics& GetStatistics();
    static void RecordIndexedDraw(uint32_t indexCount);

private:
    static RendererStatistics s_Statistics;
    static RendererCameraContext s_CameraContext;
};

}