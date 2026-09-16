// Material.cpp
#include "Raven/Renderer/RendererAPI.h"
#include "Raven/Renderer/RenderCommand.h"
#include "Raven/Renderer/Material/Material.h"
#include "Raven/Renderer/Shader/Shader.h"
#include "Raven/Renderer/Texture/Texture.h"
#include "Raven/Renderer/Pipeline/Pipeline.h"

namespace Raven
{

void Material::SetTexture(const std::string& name, Ref<Texture> texture, int slot) 
{
    m_textures[name] = TextureBinding{ std::move(texture), slot };
    m_uniforms[name] = slot;
}

void Material::SetShader(Ref<Shader> shader) 
{
    m_shader = std::move(shader);
}

Ref<Shader> Material::GetShader() const 
{
#if 1
    if (m_pipeline == nullptr) {
        return nullptr;
    }
    return m_pipeline->GetShader();
#else
    return m_shader;
#endif
}

void Material::SetPipeline(Ref<Pipeline> pipeline)
{
    m_pipeline = std::move(pipeline);
    m_SurfacePipeline = nullptr;
}

const Ref<Pipeline>& Material::GetPipeline() const
{
    return m_pipeline;
}

void Material::SetSurfaceType(MaterialSurfaceType surfaceType)
{
    m_SurfaceType = surfaceType;
    m_SurfaceTypeExplicit = true;
    m_SurfacePipeline = nullptr;
}

MaterialSurfaceType Material::GetSurfaceType() const
{
    return m_SurfaceType;
}

Ref<Pipeline> Material::ResolveSurfacePipeline() const
{
    if (m_pipeline == nullptr)
    {
        return nullptr;
    }

    const PipelineSpecification& sourceSpecification = m_pipeline->GetSpecification();

    const bool transparent = m_SurfaceType == MaterialSurfaceType::Transparent;
    const bool desiredDepthWrite = transparent == false;
    const bool desiredBlend = transparent;

    // Materialが既にPass契約どおりのPipelineを持っている場合は、そのまま利用します。
    if (sourceSpecification.DepthWrite == desiredDepthWrite
        && sourceSpecification.Blend == desiredBlend)
    {
        return m_pipeline;
    }

    if (m_SurfacePipeline != nullptr)
    {
        return m_SurfacePipeline;
    }

    // Surface分類はGraphics API固有stateではありません。
    // PipelineSpecificationを複製してDepthWrite/BlendだけをPass契約へ合わせることで、
    // OpenGLではglDepthMask/glBlend、D3D12/Vulkanでは対応PSO stateへ各Backendが変換できます。
    PipelineSpecification surfaceSpecification = sourceSpecification;
    surfaceSpecification.DepthWrite = desiredDepthWrite;
    surfaceSpecification.Blend = desiredBlend;

    m_SurfacePipeline = Pipeline::Create(surfaceSpecification);
    return m_SurfacePipeline;
}

void Material::Bind(RendererAPI& api) const
{
#if 1
    if (m_pipeline == nullptr) {
        return;
    }

    // Pipeline / Texture / Uniformを同じRHICommandListへ集約することで、
    // Materialの描画Resource設定がRendererAPI内部stateへ依存しない経路へ移行します。
    RenderCommand::BindPipeline(m_pipeline);

    for (const auto& [name, binding] : m_textures)
    {
        if (binding.texture == nullptr) {
            continue;
        }

        RenderCommand::BindTexture(name, binding.texture, binding.slot);
    }

    for (const auto& [name, value] : m_uniforms)
    {
        RenderCommand::UploadUniform(name, value);
    }
#else
    api.BindShader(m_shader);

    for (const auto& [name, binding] : m_textures)
    {
        api.BindTexture(name, binding.texture, binding.slot);
    }

    for (const auto& [name, value] : m_uniforms)
    {
        api.UploadUniform(name, value);
    }
#endif
}

void Material::BindForSurface(RendererAPI& api) const
{
    Ref<Pipeline> surfacePipeline = ResolveSurfacePipeline();
    if (surfacePipeline == nullptr)
    {
        return;
    }

    // Surface Pipelineも通常Pipelineと同じCommandListへbindし、Opaque / Transparentごとの
    // DepthWrite / Blend stateとPrimitiveTopologyを後続Resource設定・Drawへ引き継ぎます。
    RenderCommand::BindPipeline(surfacePipeline);

    for (const auto& [name, binding] : m_textures)
    {
        if (binding.texture == nullptr)
        {
            continue;
        }

        RenderCommand::BindTexture(name, binding.texture, binding.slot);
    }

    for (const auto& [name, value] : m_uniforms)
    {
        RenderCommand::UploadUniform(name, value);
    }
}

void Material::Bind() const 
{
#if 0
    if (m_shader == nullptr) return;

    m_shader.Bind();

    for (const auto& [name, binding] : m_textures) {
        if (binding.texture == nullptr) continue;

        binding.texture->Bind(binding.slot);
        m_shader.SetInt(name, binding.slot);
    }

    for (const auto& [name, value] : m_uniforms) {
        std::visit([&](const auto& v) {
            using T = std::decay_t<decltype(v)>;

            if constexpr (std::is_same_v<T, int>) {
                m_shader.SetInt(name, v);
            }
            else if constexpr (std::is_same_v<T, float>) {
                m_shader.SetFloat(name, v);
            }
            else if constexpr (std::is_same_v<T, math::Vec2>) {
                m_shader.SetVec2(name, v);
            }
            else if constexpr (std::is_same_v<T, math::Vec3>) {
                m_shader.SetVec3(name, v);
            }
            else if constexpr (std::is_same_v<T, math::Vec4>) {
                m_shader.SetVec4(name, v);
            }
            else if constexpr (std::is_same_v<T, math::Mat4>) {
                m_shader.SetMat4(name, v);
            }
        }, value);
    }
#endif

}

}
