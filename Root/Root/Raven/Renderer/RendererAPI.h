#pragma once

#include <variant>

#include "../Math/Math.h"
#include "../Math/MathVector.h"
#include "../Math/MathMatrix.h"
#include "Buffer/VertexArray.h"

namespace Raven
{

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

    virtual void Init() = 0;
    virtual void SetClearColor(float r, float g, float b, float a) = 0;
    virtual void Clear() = 0;
    virtual void DrawIndexed(const Ref<VertexArray>& vertexArray) = 0;

    virtual void BindShader(const std::shared_ptr<Shader>& shader) = 0;
    virtual void BindTexture(const std::string& name,const std::shared_ptr<Texture>& texture,uint32_t slot) = 0;
    virtual void UploadUniform(const std::string& name,const UniformValue& value) = 0;

    static API GetAPI();

private:
    static API s_API;

};

}