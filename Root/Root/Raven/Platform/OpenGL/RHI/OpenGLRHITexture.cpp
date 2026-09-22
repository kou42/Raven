#include "Raven/Platform/OpenGL/RHI/OpenGLRHITexture.h"

#include <cassert>

#include <glad/glad.h>

namespace Raven
{
namespace
{

struct OpenGLTextureFormatInfo
{
    GLint InternalFormat = GL_NONE;
    GLenum DataFormat = GL_NONE;
    GLenum DataType = GL_NONE;
};

OpenGLTextureFormatInfo ToOpenGLTextureFormat(RHITextureFormat format)
{
    switch (format)
    {
    case RHITextureFormat::R8:
        return { GL_R8, GL_RED, GL_UNSIGNED_BYTE };
    case RHITextureFormat::RGB8:
        return { GL_RGB8, GL_RGB, GL_UNSIGNED_BYTE };
    case RHITextureFormat::RGBA8:
        return { GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE };
    case RHITextureFormat::R32I:
        return { GL_R32I, GL_RED_INTEGER, GL_INT };
    case RHITextureFormat::Depth24Stencil8:
        return { GL_DEPTH24_STENCIL8, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8 };
    case RHITextureFormat::None:
    default:
        return {};
    }
}

} // namespace

OpenGLRHITexture::OpenGLRHITexture(
    const RHITextureSpecification& specification,
    const void* initialData,
    std::size_t initialDataSize)
    : m_Specification(specification)
{
    Invalidate(initialData, initialDataSize);
}

OpenGLRHITexture::~OpenGLRHITexture()
{
    if (m_RendererID != 0)
    {
        glDeleteTextures(1, &m_RendererID);
        m_RendererID = 0;
    }
}

void OpenGLRHITexture::SetData(const void* data, std::size_t dataSize)
{
    (void)TrySetData(data, dataSize);
}

bool OpenGLRHITexture::TrySetData(const void* data, std::size_t dataSize)
{
    if (data == nullptr || m_RendererID == 0)
    {
        return false;
    }

    const std::uint32_t bytesPerPixel = GetBytesPerPixel(m_Specification.Format);
    const std::size_t requiredSize = static_cast<std::size_t>(m_Specification.Width)
        * static_cast<std::size_t>(m_Specification.Height)
        * bytesPerPixel;
    if (bytesPerPixel == 0 || dataSize != requiredSize)
    {
        return;
    }

    const OpenGLTextureFormatInfo format = ToOpenGLTextureFormat(m_Specification.Format);
    if (format.DataFormat == GL_NONE)
    {
        return;
    }

    if (glGetError() != GL_NO_ERROR)
    {
        return false;
    }
    glBindTexture(GL_TEXTURE_2D, m_RendererID);
    glTexSubImage2D(
        GL_TEXTURE_2D,
        0,
        0,
        0,
        static_cast<GLsizei>(m_Specification.Width),
        static_cast<GLsizei>(m_Specification.Height),
        format.DataFormat,
        format.DataType,
        data);

    if (m_Specification.GenerateMips)
    {
        glGenerateMipmap(GL_TEXTURE_2D);
    }
    // OpenGLのerror flagはContext共有状態です。既存エラーを誤帰属しないよう、
    // この呼び出し開始時にエラーがある場合は転送を実行せず失敗とします。
    return glGetError() == GL_NO_ERROR;
}

const RHITextureSpecification& OpenGLRHITexture::GetSpecification() const
{
    return m_Specification;
}

unsigned int OpenGLRHITexture::GetRendererID() const
{
    return m_RendererID;
}

void OpenGLRHITexture::Invalidate(const void* initialData, std::size_t initialDataSize)
{
    const OpenGLTextureFormatInfo format = ToOpenGLTextureFormat(m_Specification.Format);
    if (m_Specification.Width == 0
        || m_Specification.Height == 0
        || format.InternalFormat == GL_NONE)
    {
        return;
    }

    glGenTextures(1, &m_RendererID);
    glBindTexture(GL_TEXTURE_2D, m_RendererID);

    // RenderTarget / DepthStencilはmipmapを必要としません。
    // Sampled TextureのみSpecificationに従ってmipmap生成を許可します。
    const bool useMips = m_Specification.Usage == RHITextureUsage::Sampled
        && m_Specification.GenerateMips;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, useMips ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    const void* uploadData = nullptr;
    if (initialData != nullptr)
    {
        const std::uint32_t bytesPerPixel = GetBytesPerPixel(m_Specification.Format);
        const std::size_t requiredSize = static_cast<std::size_t>(m_Specification.Width)
            * static_cast<std::size_t>(m_Specification.Height)
            * bytesPerPixel;
        if (bytesPerPixel == 0 || initialDataSize != requiredSize)
        {
            assert(false && "Initial RHI Texture data size does not match specification");
        }
        else
        {
            uploadData = initialData;
        }
    }

    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        format.InternalFormat,
        static_cast<GLsizei>(m_Specification.Width),
        static_cast<GLsizei>(m_Specification.Height),
        0,
        format.DataFormat,
        format.DataType,
        uploadData);

    if (uploadData != nullptr && useMips)
    {
        glGenerateMipmap(GL_TEXTURE_2D);
    }
}

std::uint32_t OpenGLRHITexture::GetBytesPerPixel(RHITextureFormat format)
{
    switch (format)
    {
    case RHITextureFormat::R8:
        return 1;
    case RHITextureFormat::RGB8:
        return 3;
    case RHITextureFormat::RGBA8:
    case RHITextureFormat::R32I:
    case RHITextureFormat::Depth24Stencil8:
        return 4;
    case RHITextureFormat::None:
    default:
        return 0;
    }
}

} // namespace Raven
