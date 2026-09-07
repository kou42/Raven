// Raven/Renderer/Material/LitMaterialFactory.cpp
#include "Raven/Renderer/Material/LitMaterialFactory.h"

#include "Raven/Renderer/Material/Material.h"
#include "Raven/Renderer/Pipeline/Pipeline.h"
#include "Raven/Renderer/Shader/Shader.h"

namespace Raven
{

Ref<Material> LitMaterialFactory::CreateDirectionalLit(
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

    // Base Color Texture/PBR接続前の最小Lit Materialです。
    // Terrain GLBの頂点Colorをそのまま活かせるようTintは白を既定値にします。
    material->SetUniform("u_Tint", math::Vec3{ 1.0f, 1.0f, 1.0f });
    material->SetUniform("u_Alpha", 1.0f);
    material->SetUniform("u_LightDirection", light.Direction);
    material->SetUniform("u_LightColor", light.Color);
    material->SetUniform("u_LightIntensity", light.Intensity);
    material->SetUniform("u_AmbientIntensity", light.AmbientIntensity);

    return material;
}

} // namespace Raven
