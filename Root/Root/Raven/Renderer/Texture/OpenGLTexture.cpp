#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

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

    case TextureFormat::None:
    default:
        return 0;
    }
}

TextureFormat TextureFormatFromChannels(int channels)
{
    switch (channels)
    {
    case 1:
        return TextureFormat::R8;

    case 3:
        return TextureFormat::RGB8;

    case 4:
        return TextureFormat::RGBA8;

    default:
        return TextureFormat::None;
    }
}

} // namespace

OpenGLTexture::OpenGLTexture(const std::string& path)
{
    // OpenGLのテクスチャ座標系に合わせるため、読み込み時に上下反転します。
    // ファイル読み込み方針はAPI実装側に閉じ込め、Texture抽象クラスには持ち込みません。
    stbi_set_flip_vertically_on_load(true);

    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char* data = stbi_load(path.c_str(), &width, &height, &channels, 0);

    if (data == nullptr)
    {
        std::cerr << "Failed to load texture: " << path << std::endl;
        return;
    }

    const TextureFormat format = TextureFormatFromChannels(channels);
    if (format == TextureFormat::None)
    {
        std::cerr << "Unsupported texture channel count: " << channels
                  << " path: " << path << std::endl;
        stbi_image_free(data);
        return;
    }

    m_Specification.Width = static_cast<std::uint32_t>(width);
    m_Specification.Height = static_cast<std::uint32_t>(height);
    m_Specification.Format = format;
    m_Specification.GenerateMips = true;

    Invalidate();

    const std::size_t dataSize =
        static_cast<std::size_t>(m_Specification.Width) *
        static_cast<std::size_t>(m_Specification.Height) *
        static_cast<std::size_t>(GetBytesPerPixel(m_Specification.Format));

    SetData(data, dataSize);
    stbi_image_free(data);
}

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

    if (internalFormat == 0 || dataFormat == 0)
    {
        std::cerr << "OpenGLTexture creation failed. Unsupported TextureFormat." << std::endl;
        return;
    }

    if (m_ID != 0)
    {
        glDeleteTextures(1, &m_ID);
        m_ID = 0;
    }

    glGenTextures(1, &m_ID);
    glBindTexture(GL_TEXTURE_2D, m_ID);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

    if (m_Specification.GenerateMips)
    {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    }
    else
    {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    }

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    // ここではGPU側の保存領域だけ確保します。
    // 実データの転送はSetData()へ分離することで、動的Textureも同じ経路で更新できます。
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        static_cast<GLint>(internalFormat),
        static_cast<GLsizei>(m_Specification.Width),
        static_cast<GLsizei>(m_Specification.Height),
        0,
        dataFormat,
        GL_UNSIGNED_BYTE,
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
    if (dataFormat == 0)
    {
        std::cerr << "OpenGLTexture::SetData failed. Unsupported TextureFormat." << std::endl;
        return;
    }

    glBindTexture(GL_TEXTURE_2D, m_ID);

    // OpenGL既定値(GL_UNPACK_ALIGNMENT = 4)のままだと、RGB8かつ1行のバイト数が
    // 4の倍数ではない画像で行境界がずれる可能性があります。
    // 1-byte alignmentへ一時変更し、R8/RGB8/RGBA8を同一経路で安全に転送します。
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
        GL_UNSIGNED_BYTE,
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

    case TextureFormat::None:
    default:
        return 0;
    }
}

}