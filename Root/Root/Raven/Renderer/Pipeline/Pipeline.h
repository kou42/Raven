#pragma once

#include <memory>

#include "Raven/Core/Base.h"

namespace Raven
{
class Shader;

enum class PrimitiveTopology
{
    None = 0,
    Triangles,
    Lines,
    Points
};

enum class CullMode
{
    None = 0,
    Front,
    Back
};

enum class FrontFace
{
    Clockwise = 0,
    CounterClockwise
};

enum class DepthCompareOperator
{
    Never = 0,
    Less,
    LessEqual,
    Equal,
    Greater,
    GreaterEqual,
    Always
};

struct PipelineSpecification
{
    Ref<Shader> Shader;

    PrimitiveTopology Topology = PrimitiveTopology::Triangles;

    CullMode Cull = CullMode::Back;

    FrontFace FrontFaceMode = FrontFace::CounterClockwise;

    DepthCompareOperator DepthCompare = DepthCompareOperator::Less;

    bool DepthTest = true;
    bool DepthWrite = true;

    bool Blend = false;

    // 将来のデバッグやキャッシュ用
    const char* DebugName = "Unnamed Pipeline";
};

class Pipeline
{
public:
    virtual ~Pipeline() = default;

    virtual void Bind() const = 0;
    virtual void Unbind() const = 0;

    virtual const PipelineSpecification& GetSpecification() const = 0;

    virtual Ref<Shader> GetShader() const = 0;

    static Ref<Pipeline> Create(const PipelineSpecification& specification);
};

}