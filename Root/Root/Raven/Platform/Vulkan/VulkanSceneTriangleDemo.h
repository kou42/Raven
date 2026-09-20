#pragma once

#include "VulkanSceneCommandList.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace Raven
{

// OpenGL Applicationを変更せずVulkan Sceneの描画経路を検証する最小Triangleです。
// 呼び出し元はVulkan Windowと、以下の入力宣言に一致するSPIR-Vを渡します。
// location 0: vec2 position / location 1: vec3 color、Fragmentはlocation 0の色を出力。
// WindowとShaderバイナリの読み込み・イベントループは呼び出し元が管理します。
class VulkanSceneTriangleDemo final
{
public:
    VulkanSceneTriangleDemo() = default;
    ~VulkanSceneTriangleDemo() { Shutdown(); }

    VulkanSceneTriangleDemo(const VulkanSceneTriangleDemo&) = delete;
    VulkanSceneTriangleDemo& operator=(const VulkanSceneTriangleDemo&) = delete;

    // Phase 1では位置・色を持つMeshを共通Pipelineで描画します。
    struct Vertex
    {
        float Position[2];
        float Color[3];
    };

    // Frame外でのみMeshを追加・削除します。追加失敗時は既存Meshを維持します。
    bool AddMesh(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices);
    bool ClearMeshes();
    std::size_t GetMeshCount() const { return m_Meshes.size(); }

    bool Init(Window& window, const RHIShaderBinary& vertexShader,
        const RHIShaderBinary& fragmentShader);
    RHIFrameResult DrawFrame();
    bool Resize(uint32_t width, uint32_t height);
    void Shutdown();
    VkExtent2D GetExtent() const { return m_Context.GetExtent(); }

private:
    bool CreatePipeline();

    Window* m_Window = nullptr;
    VulkanSceneContext m_Context;
    struct Mesh
    {
        Ref<RHIBuffer> VertexBuffer;
        Ref<RHIBuffer> IndexBuffer;
        uint32_t IndexCount = 0;
    };
    // Mesh数は固定せず、Bufferの所有権はRefで保持します。
    std::vector<Mesh> m_Meshes;
    Ref<RHIGraphicsPipeline> m_Pipeline;
    RHIShaderBinary m_VertexShader;
    RHIShaderBinary m_FragmentShader;
};

} // namespace Raven
