#pragma once

#include "Raven/Renderer/Texture/Texture.h"

namespace Raven
{

// OpenGL固有のTexture実装です。
// Textureインターフェースを継承し、OpenGL APIへの依存をこのクラス内に閉じ込めます。
class OpenGLTexture final : public Texture
{
public:
    explicit OpenGLTexture(const std::string& path);
    explicit OpenGLTexture(const TextureSpecification& specification);
    ~OpenGLTexture() override;

    void Bind(unsigned int slot = 0) const override;
    void Unbind() const override;
    void SetData(const void* data, std::size_t dataSize) override;

    unsigned int GetID() const override;
    int GetWidth() const override;
    int GetHeight() const override;
    const TextureSpecification& GetSpecification() const override;

private:
    // SpecificationをOpenGL Texture Objectへ反映します。
    // OpenGLのinternal format / data format変換もここだけで行います。
    void Invalidate();

    static std::uint32_t GetBytesPerPixel(TextureFormat format);

private:
    unsigned int m_ID = 0;
    TextureSpecification m_Specification;
};

}