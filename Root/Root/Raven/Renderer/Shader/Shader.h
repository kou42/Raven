#pragma once

#include <string>
#include <memory>
#include <unordered_map>

#include "../../Core/Base.h"
#include "../../Math/Math.h"
#include "../../Math/MathVector.h"
#include "../../Math/MathMatrix.h"

namespace Raven
{

class Shader
{
public:
    virtual ~Shader() = default;

    virtual void Bind() const = 0;
    virtual void Unbind() const = 0;

    virtual void SetInt(const std::string& name, int value) = 0;
    virtual void SetFloat(const std::string& name, float value) = 0;
    virtual void SetFloat2(const std::string& name, float x, float y) { return; }
    virtual void SetFloat3(const std::string& name, float x, float y, float z) = 0;
    virtual void SetFloat4(const std::string& name, float x, float y, float z, float w) = 0;
    virtual void SetVec2(const std::string& name, const math::Vec2& vec2) { return; }
    virtual void SetVec3(const std::string& name, const math::Vec3& vec3) { return; }
    virtual void SetVec4(const std::string& name, const math::Vec4& vec4) { return; }
    virtual void SetMat4(const std::string& name, const math::Mat4& mat4) { return; }

    static Ref<Shader> Create(const std::string& filepath);
    static Ref<Shader> Create(const std::string& vertFilePath, const std::string& fragFilePath);

};

class ShaderLibrary
{
public:
    void Add(const std::string& name, const Ref<Shader>& shader);

    Ref<Shader> Load(const std::string& name, const std::string& filepath);
    Ref<Shader> Load(const std::string& name, const std::string& vertexFilePath, const std::string& fragmentFilePath);

    Ref<Shader> Get(const std::string& name);
    bool Exists(const std::string& name) const;

private:
    std::unordered_map<std::string, Ref<Shader>> m_Shaders;
};

}