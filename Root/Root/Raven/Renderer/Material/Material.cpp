// Material.cpp
#include "Raven/Renderer/RendererAPI.h"
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
    if (!m_pipeline) {
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
}

const Ref<Pipeline>& Material::GetPipeline() const
{
    return m_pipeline;
}

void Material::Bind(RendererAPI& api) const
{
#if 1
    if (!m_pipeline) {
        return;
    }

    api.BindPipeline(m_pipeline);

    for (const auto& [name, binding] : m_textures)
    {
        if (!binding.texture) {
            continue;
        }

        api.BindTexture(name, binding.texture, binding.slot);
    }

    for (const auto& [name, value] : m_uniforms)
    {
        api.UploadUniform(name, value);
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

void Material::Bind() const 
{
#if 0
    if (!m_shader) return;

    m_shader.Bind();

    for (const auto& [name, binding] : m_textures) {
        if (!binding.texture) continue;

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