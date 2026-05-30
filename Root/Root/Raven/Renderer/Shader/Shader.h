#pragma once

#include <string>
#include <memory>
#include <unordered_map>

#include "../../Core/Base.h"

namespace Raven
{

class Shader
{
public:
    virtual ~Shader() = default;

    virtual void Bind() const = 0;
    virtual void Unbind() const = 0;

    virtual void SetInt(const std::string& name, int value) = 0;
    virtual void SetFloat4(const std::string& name, float x, float y, float z, float w) = 0;

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