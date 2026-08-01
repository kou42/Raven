#pragma once

#include <string>
#include <unordered_map>

#include "Raven/Core/Base.h"

// Spriteは「画像」ではない
// →Texture + Transform + Render情報などの組み合わせ

// 要検討
// Sprite　
//↓
//RenderComponent
//↓
//Renderer2D
//↓
//BatchRenderer
//↓
//GPU
//という設計になることが多いらしいです。

namespace Raven
{

class Texture
{

public:

    static Ref<Texture> Create(const std::string& path);

    Texture(const std::string& path);
    ~Texture();

    void Bind(unsigned int slot = 0) const;
    void Unbind() const;

    unsigned int GetID() const;
    
private:
    unsigned int m_ID;
    int m_Width;
    int m_Height;
    int m_Channels;

};

class TextureLibrary
{
public:
    void Add(const std::string& name, const Ref<Texture>& texture);

    Ref<Texture> Load(const std::string& name, const std::string& path);

    Ref<Texture> Get(const std::string& name);
    bool Exists(const std::string& name) const;

private:
    std::unordered_map<std::string, Ref<Texture>> m_Textures;
};

}