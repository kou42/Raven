#pragma once

#include "VulkanSceneCommandList.h"
#include "Raven/Scene/SceneCamera.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace Raven
{

// OpenGL Applicationを変更せずVulkan Sceneの描画経路を検証する最小Triangleです。
// 呼び出し元はVulkan Windowと、以下の入力宣言に一致するSPIR-Vを渡します。
// location 0: vec3 position / location 1: vec3 color、Fragmentはlocation 0の色を出力。
// WindowとShaderバイナリの読み込み・イベントループは呼び出し元が管理します。
class VulkanSceneTriangleDemo final
{
public:
    VulkanSceneTriangleDemo() = default;
    ~VulkanSceneTriangleDemo() { Shutdown(); }

    VulkanSceneTriangleDemo(const VulkanSceneTriangleDemo&) = delete;
    VulkanSceneTriangleDemo& operator=(const VulkanSceneTriangleDemo&) = delete;

    // 3D位置・色を持つMeshを共通Pipelineで描画します。
    struct Vertex
    {
        float Position[3];
        float Color[3];
    };

    // Frame外でのみMeshを追加・削除します。追加失敗時は既存Meshを維持します。
    bool AddMesh(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices);
    // 行列はGLSL mat4と同じcolumn-major順。Mesh番号は登録順です。
    bool SetMeshTransform(std::size_t meshIndex, const std::array<float, 16>& model);
    // Tintは頂点色に乗算します。AlphaBlendは透明Pipelineを使用します。
    bool SetMeshMaterial(std::size_t meshIndex, const std::array<float, 4>& tint,
        bool alphaBlend = false);
    // 既存Cameraを借用せず値で保持し、Viewport変更時にProjectionを再計算します。
    SceneCamera& GetCamera() { return m_Camera; }
    const SceneCamera& GetCamera() const { return m_Camera; }
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
    SceneCamera m_Camera;
    struct Mesh
    {
        Ref<RHIBuffer> VertexBuffer;
        Ref<RHIBuffer> IndexBuffer;
        uint32_t IndexCount = 0;
        std::array<float, 4> Tint = {1.0f, 1.0f, 1.0f, 1.0f};
        bool AlphaBlend = false;
        std::array<float, 16> Model = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f
        };
    };
    // Mesh数は固定せず、Bufferの所有権はRefで保持します。
    std::vector<Mesh> m_Meshes;
    Ref<RHIGraphicsPipeline> m_Pipeline;
    Ref<RHIGraphicsPipeline> m_TransparentPipeline;
    RHIShaderBinary m_VertexShader;
    RHIShaderBinary m_FragmentShader;
};

} // namespace Raven
