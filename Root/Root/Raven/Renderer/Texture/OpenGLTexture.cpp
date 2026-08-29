#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include "Raven/Renderer/Texture/OpenGLTexture.h"

#include <glad/glad.h>

#include <iostream>

namespace Raven
{

OpenGLTexture::OpenGLTexture(const std::string& path)
{
    glGenTextures(1, &m_ID);
    glBindTexture(GL_TEXTURE_2D, m_ID);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    // OpenGLのテクスチャ座標系に合わせるため、読み込み時に上下反転します。
    // この設定もOpenGL固有の読み込み方針として具象クラス側で管理します。
    stbi_set_flip_vertically_on_load(true);

    unsigned char* data = stbi_load(path.c_str(), &m_Width, &m_Height, &m_Channels, 0);

    if (data != nullptr)
    {
        GLenum format = GL_RGB;

        if (m_Channels == 1)
        {
            format = GL_RED;
        }
        else if (m_Channels == 3)
        {
            format = GL_RGB;
        }
        else if (m_Channels == 4)
        {
            format = GL_RGBA;
        }

        glTexImage2D(
            GL_TEXTURE_2D,
            0,
            format,
            m_Width,
            m_Height,
            0,
            format,
            GL_UNSIGNED_BYTE,
            data
        );

        glGenerateMipmap(GL_TEXTURE_2D);
    }
    else
    {
        std::cerr << "Failed to load texture: " << path << std::endl;
    }

    stbi_image_free(data);
}

OpenGLTexture::~OpenGLTexture()
{
    if (m_ID != 0)
    {
        glDeleteTextures(1, &m_ID);
        m_ID = 0;
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
    return m_Width;
}

int OpenGLTexture::GetHeight() const
{
    return m_Height;
}

}