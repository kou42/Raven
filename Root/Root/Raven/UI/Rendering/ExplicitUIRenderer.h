#pragma once

#include "Raven/Core/Base.h"
#include "Raven/Renderer/RHI/RHIBuffer.h"
#include "Raven/Renderer/RHI/RHIGraphicsPipeline.h"
#include "Raven/Renderer/RHI/RHISceneFrameLifecycle.h"
#include "Raven/UI/Rendering/UITessellator.h"

#include <unordered_map>

namespace Raven
{
class RHIDevice;
class RHITexture;
class TextureAsset;

struct PreparedExplicitUI
{
    Ref<RHIBuffer> VertexBuffer;
    Ref<RHIBuffer> IndexBuffer;
    std::vector<UITessellatedCommand> Commands;
    std::vector<Ref<RHITexture>> Textures;
    uint32_t ViewportWidth = 0;
    uint32_t ViewportHeight = 0;
    RHIBackend Backend = RHIBackend::None;
};

class ExplicitUIRenderer
{
public:
    static bool CreatePipeline(RHIDevice& device,
        const RHIShaderBinary& vertexShader,
        const RHIShaderBinary& fragmentShader,
        Ref<RHIGraphicsPipeline>& outPipeline);

    bool Prepare(RHIDevice& device, const UIDrawList& drawList,
        uint32_t viewportWidth, uint32_t viewportHeight,
        const Ref<RHITexture>& defaultTexture,
        const Ref<RHIGraphicsPipeline>& pipeline,
        PreparedExplicitUI& outUI);

    bool Draw(RHISceneCommandList& commands,
        const Ref<RHIGraphicsPipeline>& pipeline,
        const PreparedExplicitUI& preparedUI) const;

    void ClearTextureCache();

private:
    Ref<RHITexture> ResolveTexture(
        RHIDevice& device, const Ref<TextureAsset>& asset,
        const Ref<RHITexture>& defaultTexture);

    std::unordered_map<const TextureAsset*, Ref<RHITexture>> m_TextureCache;
    // raw pointer keyの再利用を防ぐため、Cache中はAsset自体の寿命も保持します。
    std::vector<Ref<TextureAsset>> m_CachedAssets;
};
} // namespace Raven
