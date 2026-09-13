// Raven/Renderer/Material/LitMaterialFactory.cpp
#include "Raven/Renderer/Material/LitMaterialFactory.h"

#include "Raven/Renderer/Material/Material.h"
#include "Raven/Renderer/Pipeline/Pipeline.h"
#include "Raven/Renderer/Shader/Shader.h"
#include "Raven/Renderer/Texture/Texture.h"

namespace Raven
{
namespace
{
Ref<Material> CreateLitMaterial(
    const math::Vec4& baseColorFactor,
    const Ref<Texture>& baseColorTexture,
    MaterialSurfaceType surfaceType,
    float alphaCutoff,
    const DirectionalLightSettings& light)
{
    Ref<Shader> shader = Shader::Create(
        "Raven/Assets/Shaders/Vertex/lit.vert",
        "Raven/Assets/Shaders/Fragment/lit.frag");
    if (shader == nullptr)
    {
        return nullptr;
    }

    PipelineSpecification pipelineSpecification{};
    pipelineSpecification.DebugName = "Directional Lit Geometry Pipeline";
    pipelineSpecification.Shader = shader;
    pipelineSpecification.Topology = PrimitiveTopology::Triangles;
    pipelineSpecification.Cull = CullMode::Back;
    pipelineSpecification.FrontFaceMode = FrontFace::CounterClockwise;
    pipelineSpecification.DepthTest = true;
    pipelineSpecification.DepthWrite = true;
    pipelineSpecification.DepthCompare = DepthCompareOperator::Less;
    pipelineSpecification.Blend = false;

    Ref<Pipeline> pipeline = Pipeline::Create(pipelineSpecification);
    if (pipeline == nullptr)
    {
        return nullptr;
    }

    Ref<Material> material = CreateRef<Material>(pipeline);
    if (material == nullptr)
    {
        return nullptr;
    }

    // SurfaceTypeはRender Queue分類の正規データです。
    // MaskedをBlend扱いにせずOpaque Passへ残すことで、cutout部分以外は通常Geometryと同様に
    // Depthを書き込みます。Transparentだけが後段のTransparent Passへ送られます。
    material->SetSurfaceType(surfaceType);

    material->SetUniform("u_BaseColorFactor", baseColorFactor);
    material->SetUniform("u_HasBaseColorTexture", baseColorTexture != nullptr ? 1 : 0);
    if (baseColorTexture != nullptr)
    {
        material->SetTexture("u_BaseColorTexture", baseColorTexture, 0);
    }

    material->SetUniform(
        "u_AlphaMaskEnabled",
        surfaceType == MaterialSurfaceType::Masked ? 1 : 0);
    material->SetUniform("u_AlphaCutoff", alphaCutoff);

    material->SetUniform("u_LightDirection", light.Direction);
    material->SetUniform("u_LightColor", light.Color);
    material->SetUniform("u_LightIntensity", light.Intensity);
    material->SetUniform("u_AmbientIntensity", light.AmbientIntensity);

    return material;
}
} // namespace

Ref<Material> LitMaterialFactory::CreateDirectionalLit(
    const DirectionalLightSettings& light)
{
    return CreateLitMaterial(
        math::Vec4{ 1.0f, 1.0f, 1.0f, 1.0f },
        nullptr,
        MaterialSurfaceType::Opaque,
        0.5f,
        light);
}

Ref<Material> LitMaterialFactory::CreateDirectionalLit(
    const math::Vec4& baseColorFactor,
    const Ref<Texture>& baseColorTexture,
    const DirectionalLightSettings& light)
{
    return CreateLitMaterial(
        baseColorFactor,
        baseColorTexture,
        MaterialSurfaceType::Opaque,
        0.5f,
        light);
}

Ref<Material> LitMaterialFactory::CreateDirectionalLit(
    const math::Vec4& baseColorFactor,
    const Ref<Texture>& baseColorTexture,
    MaterialSurfaceType surfaceType,
    float alphaCutoff,
    const DirectionalLightSettings& light)
{
    return CreateLitMaterial(
        baseColorFactor,
        baseColorTexture,
        surfaceType,
        alphaCutoff,
        light);
}

} // namespace Raven
