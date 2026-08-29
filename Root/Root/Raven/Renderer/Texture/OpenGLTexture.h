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
    ~OpenGLTexture() override;

    void Bind(unsigned int slot = 0) const override;
    void Unbind() const override;

    unsigned int GetID() const override;
    int GetWidth() const override;
    int GetHeight() const override;

private:
    unsigned int m_ID = 0;
    int m_Width = 0;
    int m_Height = 0;
    int m_Channels = 0;
};

}