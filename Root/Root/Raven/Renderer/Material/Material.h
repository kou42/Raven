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

class Pipeline;
class Shader;
class Texture;

class Material {

public:

    explicit Material(Ref<Pipeline> pipeline) {
        m_pipeline = std::move(pipeline);
    }
    explicit Material(Ref<Shader> shader) {
        m_shader = std::move(shader);
    }

    void SetPipeline(Ref<Pipeline> pipeline);
    const Ref<Pipeline>& GetPipeline() const;

    void SetTexture(const std::string& name, Ref<Texture> texture, int slot);

    void SetShader(Ref<Shader> shader);
    Ref<Shader> GetShader() const;

    void Bind() const;
    void Bind(RendererAPI& api) const;

    template<class T>
    void SetUniform(const std::string& name, const T& value) {
        m_uniforms[name] = value;
    }

private:
    struct TextureBinding {
        //std::shared_ptr<Texture2D> texture;
        Ref<Texture> texture;
        int slot = 0;
    };

private:
    Ref<Pipeline> m_pipeline;
    Ref<Shader> m_shader;
    std::unordered_map<std::string, TextureBinding> m_textures;
    std::unordered_map<std::string, UniformValue> m_uniforms;
};

}