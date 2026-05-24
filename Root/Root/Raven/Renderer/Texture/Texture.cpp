#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include "Texture.h"
#include <glad/glad.h>
#include <string>
#include <iostream>

#include "../../Core/Base.h"
#include "../RendererAPI.h"

namespace Raven
{

Ref<Texture> Texture::Create(const std::string& path)
{
    switch (RendererAPI::GetAPI())
    {
    case RendererAPI::API::OpenGL:
        return CreateRef<Texture>(path);

    case RendererAPI::API::DirectX11:
        // return CreateRef<DirectX11Shader>(filepath);
        return nullptr;
    case RendererAPI::API::DirectX12:
        // return CreateRef<DirectX12Shader>(filepath);
        return nullptr;
    }

    return nullptr;
}

Texture::Texture(const std::string& path)
{
    glGenTextures(1, &m_ID);
    glBindTexture(GL_TEXTURE_2D, m_ID);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    stbi_set_flip_vertically_on_load(true);

    unsigned char* data = stbi_load(path.c_str(), &m_Width, &m_Height, &m_Channels, 0);

    if (data)
    {
        GLenum format = GL_RGB;

        if (m_Channels == 1) format = GL_RED;
        else if (m_Channels == 3) format = GL_RGB;
        else if (m_Channels == 4) format = GL_RGBA;

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
        std::cout << "Failed to load texture: " << path << std::endl;
    }

    stbi_image_free(data);
}

Texture::~Texture()
{
    glDeleteTextures(1, &m_ID);
}

void Texture::Bind(unsigned int slot) const
{
    glActiveTexture(GL_TEXTURE0 + slot);
    glBindTexture(GL_TEXTURE_2D, m_ID);
}

void Texture::Unbind() const
{
    glBindTexture(GL_TEXTURE_2D, 0);
}

unsigned int Texture::GetID() const
{
    return m_ID;
}

}