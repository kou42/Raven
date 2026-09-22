#pragma once

#include "Raven/Renderer/Texture/Texture.h"
#include "Raven/Renderer/RHI/RHITexture.h"

namespace Raven
{

// OpenGL固有のTexture互換実装です。
// GPU Resource本体はOpenGLRHITextureへ移し、このクラスは既存Texture APIとRHIのBridgeを担当します。
// Source AssetのdecodeはImporter側で完了させ、このクラスはRHI Resource生成だけを担当します。
class OpenGLTexture final : public Texture
{
public:
    explicit OpenGLTexture(const TextureSpecification& specification);
    ~OpenGLTexture() override = default;

    void Bind(unsigned int slot = 0) const override;
    void Unbind() const override;
    void SetData(const void* data, std::size_t dataSize) override;
    bool TrySetData(const void* data, std::size_t dataSize) override;

    unsigned int GetID() const override;
    int GetWidth() const override;
    int GetHeight() const override;
    const TextureSpecification& GetSpecification() const override;

private:
    // Legacy TextureSpecificationをRHI共通Specificationへ変換します。
    // Graphics API固有値への変換はOpenGLRHITextureだけが担当します。
    static RHITextureSpecification ToRHISpecification(const TextureSpecification& specification);

private:
    TextureSpecification m_Specification;
    Ref<RHITexture> m_RHITexture;
};

}