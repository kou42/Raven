#include "Raven/Renderer/Texture/OpenGLTexture.h"

#include <glad/glad.h>

#include <iostream>

namespace Raven
{

namespace
{

GLenum ToOpenGLInternalFormat(TextureFormat format)
{
    switch (format)
    {
    case TextureFormat::R8:
        return GL_R8;

    case TextureFormat::RGB8:
        return GL_RGB8;

    case TextureFormat::RGBA8:
        return GL_RGBA8;

    case TextureFormat::R32I:
        return GL_R32I;

    case TextureFormat::Depth24Stencil8:
        return GL_DEPTH24_STENCIL8;

    case TextureFormat::None:
    default:
        return 0;
    }
}

GLenum ToOpenGLDataFormat(TextureFormat format)
{
    switch (format)
    {
    case TextureFormat::R8:
        return GL_RED;

    case TextureFormat::RGB8:
        return GL_RGB;

    case TextureFormat::RGBA8:
        return GL_RGBA;

    case TextureFormat::R32I:
        // 整数TextureはGL_REDではなくGL_RED_INTEGERを使用します。
        // これをGL_REDにすると整数RenderTargetとして正しく読み書きできません。
        return GL_RED_INTEGER;

    case TextureFormat::Depth24Stencil8:
        return GL_DEPTH_STENCIL;

    case TextureFormat::None:
    default:
        return 0;
    }
}

GLenum ToOpenGLDataType(TextureFormat format)
{
    switch (format)
    {
    case TextureFormat::R32I:
        return GL_INT;

    case TextureFormat::Depth24Stencil8:
        return GL_UNSIGNED_INT_24_8;

    case TextureFormat::R8:
    case TextureFormat::RGB8:
    case TextureFormat::RGBA8:
        return GL_UNSIGNED_BYTE;

    case TextureFormat::None:
    default:
        return 0;
    }
}

} // namespace

OpenGLTexture::OpenGLTexture(const TextureSpecification& specification)
    : m_Specification(specification)
{
    Invalidate();
}

OpenGLTexture::~OpenGLTexture()
{
    if (m_ID != 0)
    {
        glDeleteTextures(1, &m_ID);
        m_ID = 0;
    }
}

void OpenGLTexture::Invalidate()
{
    if (m_Specification.Width == 0 || m_Specification.Height == 0)
    {
        std::cerr << "OpenGLTexture creation failed. Width and height must be greater than zero." << std::endl;
        return;
    }

    const GLenum internalFormat = ToOpenGLInternalFormat(m_Specification.Format);
    const GLenum dataFormat = ToOpenGLDataFormat(m_Specification.Format);
    const GLenum dataType = ToOpenGLDataType(m_Specification.Format);

    if (internalFormat == 0 || dataFormat == 0 || dataType == 0)
    {
        std::cerr << "OpenGLTexture creation failed. Unsupported TextureFormat." << std::endl;
        return;
    }

    if (m_Specification.Usage == TextureUsage::DepthStencil &&
        m_Specification.Format != TextureFormat::Depth24Stencil8)
    {
        std::cerr << "OpenGLTexture creation failed. DepthStencil usage requires a depth/stencil format." << std::endl;
        return;
    }

    if (m_Specification.Format == TextureFormat::Depth24Stencil8 &&
        m_Specification.Usage != TextureUsage::DepthStencil)
    {
        std::cerr << "OpenGLTexture creation failed. Depth24Stencil8 format requires DepthStencil usage." << std::endl;
        return;
    }

    if (m_ID != 0)
    {
        glDeleteTextures(1, &m_ID);
        m_ID = 0;
    }

    glGenTextures(1, &m_ID);
    glBindTexture(GL_TEXTURE_2D, m_ID);

    // Sampled Textureは繰り返しを既定値とし、Framebuffer Attachmentは境界を越えて
    // samplingした際に隣接値を拾わないようClampToEdgeを使用します。
    if (m_Specification.Usage == TextureUsage::Sampled)
    {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    }
    else
    {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }

    // Depth/Stencil Textureと整数ID Textureでは線形補間の意味がありません。
    // Entity IDが隣接pixelと混ざるとPicking結果が壊れるためR32IもNearest固定にします。
    if (m_Specification.Usage == TextureUsage::DepthStencil ||
        m_Specification.Format == TextureFormat::R32I)
    {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    }
    else if (m_Specification.GenerateMips)
    {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    }
    else
    {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    }

    // RenderTarget / DepthStencil TextureはGPU側の保存領域だけ確保します。
    // Sampled Textureも同じ経路で領域を作り、必要ならSetData()で初期データを転送します。
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        static_cast<GLint>(internalFormat),
        static_cast<GLsizei>(m_Specification.Width),
        static_cast<GLsizei>(m_Specification.Height),
        0,
        dataFormat,
        dataType,
        nullptr
    );
}

void OpenGLTexture::SetData(const void* data, std::size_t dataSize)
{
    if (m_ID == 0)
    {
        std::cerr << "OpenGLTexture::SetData failed. Texture is not initialized." << std::endl;
        return;
    }

    if (data == nullptr)
    {
        std::cerr << "OpenGLTexture::SetData failed. Data is null." << std::endl;
        return;
    }

    // Depth/Stencil TextureはFramebufferへの描画結果をGPUが書き込む用途です。
    // 通常のColor pixel uploadと同じAPIで扱うと誤用しやすいため、現段階では明示的に禁止します。
    if (m_Specification.Usage == TextureUsage::DepthStencil)
    {
        std::cerr << "OpenGLTexture::SetData failed. DepthStencil textures cannot be updated through SetData()." << std::endl;
        return;
    }

    const std::size_t expectedSize =
        static_cast<std::size_t>(m_Specification.Width) *
        static_cast<std::size_t>(m_Specification.Height) *
        static_cast<std::size_t>(GetBytesPerPixel(m_Specification.Format));

    if (dataSize != expectedSize)
    {
        std::cerr << "OpenGLTexture::SetData failed. Invalid data size. expected="
                  << expectedSize << " actual=" << dataSize << std::endl;
        return;
    }

    const GLenum dataFormat = ToOpenGLDataFormat(m_Specification.Format);
    const GLenum dataType = ToOpenGLDataType(m_Specification.Format);
    if (dataFormat == 0 || dataType == 0)
    {
        std::cerr << "OpenGLTexture::SetData failed. Unsupported TextureFormat." << std::endl;
        return;
    }

    glBindTexture(GL_TEXTURE_2D, m_ID);

    // OpenGL既定値(GL_UNPACK_ALIGNMENT = 4)のままだと、RGB8かつ1行のバイト数が
    // 4の倍数ではない画像で行境界がずれる可能性があります。
    GLint previousUnpackAlignment = 0;
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &previousUnpackAlignment);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    glTexSubImage2D(
        GL_TEXTURE_2D,
        0,
        0,
        0,
        static_cast<GLsizei>(m_Specification.Width),
        static_cast<GLsizei>(m_Specification.Height),
        dataFormat,
        dataType,
        data
    );

    glPixelStorei(GL_UNPACK_ALIGNMENT, previousUnpackAlignment);

    if (m_Specification.GenerateMips)
    {
        glGenerateMipmap(GL_TEXTURE_2D);
    }
}

void OpenGLTexture::Bind(unsigned int slot) const
{
    glActiveTexture(GL_TEXTURE0 + slot);
    glBindTexture(GL_TEXTURE_2D, m_ID);
}

void OpenGLTexture::Unbind() const
{
    glBindTexture(GL_TEXTURE_2D, 0);
}

unsigned int OpenGLTexture::GetID() const
{
    return m_ID;
}

int OpenGLTexture::GetWidth() const
{
    return static_cast<int>(m_Specification.Width);
}

int OpenGLTexture::GetHeight() const
{
    return static_cast<int>(m_Specification.Height);
}

const TextureSpecification& OpenGLTexture::GetSpecification() const
{
    return m_Specification;
}

std::uint32_t OpenGLTexture::GetBytesPerPixel(TextureFormat format)
{
    switch (format)
    {
    case TextureFormat::R8:
        return 1;

    case TextureFormat::RGB8:
        return 3;

    case TextureFormat::RGBA8:
        return 4;

    case TextureFormat::R32I:
        return 4;

    case TextureFormat::Depth24Stencil8:
        return 4;

    case TextureFormat::None:
    default:
        return 0;
    }
}

}