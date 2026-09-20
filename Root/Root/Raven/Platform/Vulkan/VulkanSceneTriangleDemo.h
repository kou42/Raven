#pragma once

#include "VulkanSceneCommandList.h"

#include <array>

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

    bool Init(Window& window, const RHIShaderBinary& vertexShader,
        const RHIShaderBinary& fragmentShader);
    RHIFrameResult DrawFrame();
    bool Resize(uint32_t width, uint32_t height);
    void Shutdown();
    VkExtent2D GetExtent() const { return m_Context.GetExtent(); }

private:
    struct Vertex
    {
        float Position[2];
        float Color[3];
    };

    bool CreatePipeline();

    Window* m_Window = nullptr;
    VulkanSceneContext m_Context;
    VulkanSceneBuffer m_VertexBuffer;
    VulkanSceneBuffer m_IndexBuffer;
    Ref<RHIGraphicsPipeline> m_Pipeline;
    RHIShaderBinary m_VertexShader;
    RHIShaderBinary m_FragmentShader;
};

} // namespace Raven
