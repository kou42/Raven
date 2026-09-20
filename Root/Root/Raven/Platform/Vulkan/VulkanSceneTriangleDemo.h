#pragma once

#include "VulkanSceneCommandList.h"
#include "Raven/Renderer/RHI/RHITexture.h"
#include "Raven/Scene/SceneCamera.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace Raven
{

// OpenGL Applicationを変更せずVulkan Sceneの描画経路を検証する最小Triangleです。
// 呼び出し元はVulkan Windowと、以下の入力宣言に一致するSPIR-Vを渡します。
// location 0: vec3 position / location 1: vec3 color / location 2: vec2 UV。
// WindowとShaderバイナリの読み込み・イベントループは呼び出し元が管理します。
class VulkanSceneTriangleDemo final
{
public:
    VulkanSceneTriangleDemo() = default;
    ~VulkanSceneTriangleDemo() { Shutdown(); }

    VulkanSceneTriangleDemo(const VulkanSceneTriangleDemo&) = delete;
    VulkanSceneTriangleDemo& operator=(const VulkanSceneTriangleDemo&) = delete;

    // 3D位置・色・UVを持つMeshを共通Pipelineで描画します。
    struct Vertex
    {
        float Position[3];
        float Color[3];
        float UV[2];
    };

    // Frame外でのみMeshを追加・削除します。追加失敗時は既存Meshを維持します。
    bool AddMesh(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices);
    // 行列はGLSL mat4と同じcolumn-major順。Mesh番号は登録順です。
    bool SetMeshTransform(std::size_t meshIndex, const std::array<float, 16>& model);
    // Tintは頂点色に乗算します。AlphaBlendは透明Pipelineを使用します。
    bool SetMeshMaterial(std::size_t meshIndex, const std::array<float, 4>& tint,
        bool alphaBlend = false);
    // Tint・Blend・Textureを一括更新します。検証失敗時は既存Materialを変更しません。
    bool SetMeshMaterial(std::size_t meshIndex, const std::array<float, 4>& tint,
        bool alphaBlend, std::size_t textureIndex);
    // Texture番号はAddTexture()の登録順です。0は既定Checker Textureです。
    // MaterialはMesh単位でTexture番号を保持し、同じ番号のGPU Textureを共有します。
    bool SetMeshTexture(std::size_t meshIndex, std::size_t textureIndex);
    // Frame外でRGBA8 Textureを追加します。Descriptor生成済みならGPU完了待ち後に再構築します。
    bool AddTexture(uint32_t width, uint32_t height, const uint8_t* rgba);
    // 同じScene Deviceから生成したRHITextureを共有登録します。登録成功時に番号を返します。
    // 別Deviceや未対応形式のTextureは受け付けません。
    bool AddTexture(const Ref<RHITexture>& texture, std::size_t& textureIndex);
    std::size_t GetTextureCount() const { return m_Textures.size(); }
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
    bool CreateTextureDescriptor();
    void DestroyTextureDescriptor();

    Window* m_Window = nullptr;
    VulkanSceneContext m_Context;
    SceneCamera m_Camera;
    struct TextureResource
    {
        Ref<RHITexture> Image;
        VkDescriptorSet Descriptor = VK_NULL_HANDLE;
    };
    // TextureResourceはSceneが所有し、Meshは登録番号だけを参照します。
    std::vector<TextureResource> m_Textures;
    VkDescriptorPool m_TextureDescriptorPool = VK_NULL_HANDLE;
    // 汎用Materialとは別のVulkan Scene検証用データ。API固有handleはTextureResourceに閉じ込めます。
    struct SceneMaterial
    {
        std::array<float, 4> Tint = {1.0f, 1.0f, 1.0f, 1.0f};
        bool AlphaBlend = false;
        std::size_t TextureIndex = 0;
    };
    struct Mesh
    {
        Ref<RHIBuffer> VertexBuffer;
        Ref<RHIBuffer> IndexBuffer;
        uint32_t IndexCount = 0;
        SceneMaterial Material;
        // 透明Meshの近似ソートに使うローカル空間の頂点平均位置です。
        std::array<float, 3> LocalCenter = {0.0f, 0.0f, 0.0f};
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
