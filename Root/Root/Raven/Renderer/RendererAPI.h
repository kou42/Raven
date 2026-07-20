#pragma once

#include <variant>

#include "Raven/Math/Math.h"
#include "Raven/Math/MathVector.h"
#include "Raven/Math/MathMatrix.h"
#include "Raven/Renderer/Buffer/VertexArray.h"

namespace Raven
{

class Pipeline;
class Shader;
class Texture;

using UniformValue = std::variant<
    int,
    float,
    math::Vec2,
    math::Vec3,
    math::Vec4,
    math::Mat4
>;

class RendererAPI
{
public:

    enum class API
    {
        None = 0,
        OpenGL,
        DirectX11,
        DirectX12,
        Vulkan
    };

public:
    virtual ~RendererAPI() = default;

    virtual void SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height) = 0;
    virtual void Init() = 0;
    virtual void SetClearColor(float r, float g, float b, float a) = 0;
    virtual void Clear() = 0;
    virtual void DrawIndexed(const Ref<VertexArray>& vertexArray, uint32_t indexCount = 0) = 0;

    virtual void BindShader(const Ref<Shader>& shader) = 0;
    virtual void BindPipeline(const Ref<Pipeline>& pipeline) = 0;
    virtual void BindTexture(const std::string& name,const Ref<Texture>& texture,uint32_t slot) = 0;
    virtual void UploadUniform(const std::string& name,const UniformValue& value) = 0;

    static API GetAPI();

private:
    static API s_API;

};

}