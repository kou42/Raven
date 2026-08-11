#pragma once

#include "Raven/Core/Base.h"

#include <cstdint>

namespace Raven
{

namespace math
{
	struct Mat4;
}

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

class Renderer
{
public:
    static void Init();
    static void Shutdown();

    static void BeginFrame();
    static void BeginScene();
    static void EndScene();

    static void Submit(const Ref<Shader>& shader, const Ref<VertexArray>& vertexArray);
    static void DrawIndexed(const Ref<VertexArray>& vertexArray);
    static void Draw(const Ref<Mesh>& mesh, const Ref<Material>& material, const math::Mat4& transform);

    static RendererAPI& GetAPI();

    static const RendererStatistics& GetStatistics();
    static void RecordIndexedDraw(uint32_t indexCount);

private:
    static RendererStatistics s_Statistics;
};

}