// Material.h
#pragma once
#include <memory>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <variant>

#include "Raven/Renderer/Shader/ShaderTypes.h"
#include "Raven/Math/Math.h"
#include "Raven/Math/MathVector.h"
#include "Raven/Math/MathMatrix.h"

namespace Raven {

class Pipeline;
class RendererAPI;
class Shader;
class Texture;

// ============================================================================
// MaterialSurfaceType
// ============================================================================
// Graphics API固有のBlend/Depth stateではなく、Materialが持つ描画上の意味を表します。
// Opaque / MaskedはOpaque PassでDepthを書き込み、TransparentだけをTransparent Passへ送ります。
// Maskedのalpha cutoff判定はFragment Shader側のdiscardで行い、Blendは使用しません。
enum class MaterialSurfaceType
{
    Opaque = 0,
    Masked,
    Transparent
};

class Material {

public:

    explicit Material(Ref<Pipeline> pipeline) {
        m_pipeline = std::move(pipeline);
    }
    explicit Material(Ref<Shader> shader) {
        m_shader = std::move(shader);
    }

    void SetPipeline(Ref<Pipeline> pipeline);
    const Ref<Pipeline>& GetPipeline() const;

    void SetSurfaceType(MaterialSurfaceType surfaceType);
    MaterialSurfaceType GetSurfaceType() const;

    void SetTexture(const std::string& name, Ref<Texture> texture, int slot);

    void SetShader(Ref<Shader> shader);
    Ref<Shader> GetShader() const;

    // RHI移行後の標準Bind経路です。MaterialはGraphics API objectを受け取らず、
    // RenderCommandを通して現在のRHICommandListへPipeline / Texture / Uniformを設定します。
    void Bind() const;

    // Physics Debug等の残存呼び出しを段階移行するための互換overloadです。
    // RendererAPIは参照せず、標準Bind()へ転送します。
    void Bind(RendererAPI& api) const;

    // 通常SceneのRender Passから利用するBindです。
    // MaterialSurfaceTypeに応じてAPI非依存のPipelineSpecificationを派生させるため、
    // OpenGL固有のDepthMask切り替えを上位Rendererへ漏らしません。
    void BindForSurface() const;

    template<class T>
    void SetUniform(const std::string& name, const T& value) {
        m_uniforms[name] = value;

        // 既存Materialの段階移行用互換処理です。
        // 現在のtest.frag系Materialはu_Alphaを共通契約としているため、明示SurfaceTypeが
        // 設定されていない場合だけAlphaから透明分類を補完します。
        // glTF等ではSetSurfaceType()を明示し、Shader Uniform名へ依存しない分類へ移行できます。
        if constexpr (std::is_same_v<std::decay_t<T>, float>)
        {
            if (m_SurfaceTypeExplicit == false && name == "u_Alpha")
            {
                const MaterialSurfaceType inferredType =
                    value < 1.0f ? MaterialSurfaceType::Transparent : MaterialSurfaceType::Opaque;

                if (m_SurfaceType != inferredType)
                {
                    m_SurfaceType = inferredType;
                    m_SurfacePipeline = nullptr;
                }
            }
        }
    }

private:
    Ref<Pipeline> ResolveSurfacePipeline() const;

    struct TextureBinding {
        //std::shared_ptr<Texture2D> texture;
        Ref<Texture> texture;
        int slot = 0;
    };

private:
    Ref<Pipeline> m_pipeline;
    Ref<Shader> m_shader;
    std::unordered_map<std::string, TextureBinding> m_textures;
    std::unordered_map<std::string, UniformValue> m_uniforms;

    MaterialSurfaceType m_SurfaceType = MaterialSurfaceType::Opaque;
    bool m_SurfaceTypeExplicit = false;

    // Explicit API(D3D12/Vulkan)でも同じ設計を使えるよう、Surfaceごとの差分は
    // PipelineSpecificationから派生Pipelineとしてキャッシュします。
    mutable Ref<Pipeline> m_SurfacePipeline;
};

}
