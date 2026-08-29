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

// Textureは描画APIに依存しないインターフェースです。
// OpenGL固有のGLuintやglBindTextureなどは派生クラス側へ閉じ込めます。
// これにより、Textureを利用する上位層はOpenGL / DirectXなどの違いを意識せずに扱えます。
class Texture
{
public:
    virtual ~Texture() = default;

    // 現在選択されているRendererAPIに対応したTexture実装を生成します。
    // 利用側はOpenGLTextureなどの具象型を直接生成しないことを基本方針とします。
    static Ref<Texture> Create(const std::string& path);

    virtual void Bind(unsigned int slot = 0) const = 0;
    virtual void Unbind() const = 0;

    // 既存コードとの互換性を維持するためRenderer側のIDを公開しています。
    // 将来的にRendererIDの直接参照をなくせる場合は、この関数自体を削除することも検討できます。
    virtual unsigned int GetID() const = 0;

    virtual int GetWidth() const = 0;
    virtual int GetHeight() const = 0;
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