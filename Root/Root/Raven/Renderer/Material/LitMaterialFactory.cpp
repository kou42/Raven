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

    material->SetUniform("u_BaseColorFactor", baseColorFactor);
    material->SetUniform("u_HasBaseColorTexture", baseColorTexture != nullptr ? 1 : 0);
    if (baseColorTexture != nullptr)
    {
        material->SetTexture("u_BaseColorTexture", baseColorTexture, 0);
    }

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
        light);
}

Ref<Material> LitMaterialFactory::CreateDirectionalLit(
    const math::Vec4& baseColorFactor,
    const Ref<Texture>& baseColorTexture,
    const DirectionalLightSettings& light)
{
    return CreateLitMaterial(baseColorFactor, baseColorTexture, light);
}

Ref<Material> LitMaterialFactory::CreateStaticSceneVisibilityDiagnostic()
{
    Ref<Shader> shader = Shader::Create(
        "Raven/Assets/Shaders/Vertex/static_scene_visibility_debug.vert",
        "Raven/Assets/Shaders/Fragment/static_scene_visibility_debug.frag");
    if (shader == nullptr)
    {
        return nullptr;
    }

    PipelineSpecification pipelineSpecification{};
    pipelineSpecification.DebugName = "Static Scene Visibility Diagnostic Pipeline";
    pipelineSpecification.Shader = shader;
    pipelineSpecification.Topology = PrimitiveTopology::Triangles;

    // glTFのwinding / FrontFace解釈が原因でも強制表示できるよう、診断中はCullを無効にします。
    // Depthは通常Sceneと同じ条件を維持し、奥行き関係そのものは壊さないようにします。
    pipelineSpecification.Cull = CullMode::None;
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

    return CreateRef<Material>(pipeline);
}

} // namespace Raven
