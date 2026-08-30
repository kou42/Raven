#pragma once

#include "Raven/UI/Rendering/UIRenderer.h"

#include <cstdint>

namespace Raven
{

class IndexBuffer;
class Shader;
class VertexArray;
class VertexBuffer;

class OpenGLUIRenderer final : public UIRenderer
{
public:
    OpenGLUIRenderer();
    virtual ~OpenGLUIRenderer() = default;

    virtual void Render(
        const UIDrawList& drawList,
        const math::Vec2& viewportSize) override;

private:
    void EnsureBuffers(
        const float* vertices,
        uint32_t vertexDataSize,
        const uint32_t* indices,
        uint32_t indexCount);

private:
    Ref<VertexArray> m_VertexArray;
    Ref<VertexBuffer> m_VertexBuffer;
    Ref<IndexBuffer> m_IndexBuffer;
    Ref<Shader> m_Shader;
};

} // namespace Raven
