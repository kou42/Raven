#pragma once

#include <string>
#include <memory>

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

}