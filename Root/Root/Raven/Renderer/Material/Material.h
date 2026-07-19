// Material.h
#pragma once
#include <memory>
#include <string>
#include <unordered_map>
#include <variant>
#include <glm/glm.hpp>

#include "Raven/Renderer/RendererAPI.h"
#include "Raven/Math/Math.h"
#include "Raven/Math/MathVector.h"
#include "Raven/Math/MathMatrix.h"

namespace Raven {

class Shader;
class Texture;

class Material {

public:

#if 0
    using UniformValue = std::variant<
        int,
        float,
        math::Vec2,
        math::Vec3,
        math::Vec4,
        math::Mat4
    >;
#endif

    explicit Material(std::shared_ptr<Shader> shader)
        : m_shader(std::move(shader)) {}

    void SetShader(std::shared_ptr<Shader> shader) {
        m_shader = std::move(shader);
    }

    void SetTexture(const std::string& name,
        //std::shared_ptr<Texture2D> texture,
        std::shared_ptr<Texture> texture,
        int slot) {
        m_textures[name] = TextureBinding{ std::move(texture), slot };
        m_uniforms[name] = slot;
    }

    template<class T>
    void SetUniform(const std::string& name, const T& value) {
        m_uniforms[name] = value;
    }

    void Bind() const;
    void Bind(RendererAPI& api) const;

    std::shared_ptr<Shader> GetShader() const {
        return m_shader;
    }

private:
    struct TextureBinding {
        //std::shared_ptr<Texture2D> texture;
        std::shared_ptr<Texture> texture;
        int slot = 0;
    };

private:
    std::shared_ptr<Shader> m_shader;
    std::unordered_map<std::string, TextureBinding> m_textures;
    std::unordered_map<std::string, UniformValue> m_uniforms;
};

}