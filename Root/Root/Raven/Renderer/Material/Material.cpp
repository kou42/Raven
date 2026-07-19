// Material.cpp
#include "Raven/Renderer/Material/Material.h"
//#include "Raven/Renderer/Shader/Shader.h"
#include "Raven/Renderer/Shader/Shader.h"
//#include "Texture2D.h"
#include "Raven/Renderer/Texture/Texture.h"

namespace Raven
{

void Material::Bind(RendererAPI& api) const
{
    api.BindShader(m_shader);

    for (const auto& [name, binding] : m_textures)
    {
        api.BindTexture(name, binding.texture, binding.slot);
    }

    for (const auto& [name, value] : m_uniforms)
    {
        api.UploadUniform(name, value);
    }
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