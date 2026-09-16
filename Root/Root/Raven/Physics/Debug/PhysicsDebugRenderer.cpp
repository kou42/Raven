#include <algorithm>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#include <glad/glad.h>

#include "Raven/Core/Input.h"
#include "Raven/Core/KeyCodes.h"
#include "Raven/Physics/Collision/DynamicAABBTreeValidation.h"
#include "Raven/Physics/Debug/PhysicsDebugOverlayFont.h"
#include "Raven/Physics/Debug/PhysicsDebugRenderer.h"
#include "Raven/Physics/PhysicsWorld.h"
#include "Raven/Renderer/Buffer/IndexBuffer.h"
#include "Raven/Renderer/Buffer/VertexArray.h"
#include "Raven/Renderer/Buffer/VertexBuffer.h"
#include "Raven/Renderer/Material/Material.h"
#include "Raven/Renderer/Mesh/Mesh.h"
#include "Raven/Renderer/Pipeline/Pipeline.h"
#include "Raven/Renderer/RenderCommand.h"
#include "Raven/Renderer/Renderer.h"
#include "Raven/Renderer/Shader/Shader.h"
#include "Raven/Scene/Scene.h"

namespace Raven::ph
{
namespace
{
std::string OnOff(bool enabled)
{
    return enabled ? "ON" : "OFF";
}

std::string FormatFloat(float value)
{
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(3) << value;
    return stream.str();
}

} // namespace

PhysicsDebugRenderer::PhysicsDebugRenderer(Scene& scene)
    : m_Scene(&scene)
{
    Registry().push_back(this);
}

PhysicsDebugRenderer::~PhysicsDebugRenderer()
{
    auto& registry = Registry();
    registry.erase(
        std::remove(registry.begin(), registry.end(), this),
        registry.end());
}

void PhysicsDebugRenderer::RenderRegistered()
{
    for (auto* renderer : Registry())
    {
        if (renderer != nullptr)
        {
            renderer->Render();
        }
    }
}

void PhysicsDebugRenderer::BindPhysicsWorld(Scene& scene, const PhysicsWorld& world)
{
    for (auto* renderer : Registry())
    {
        if (renderer != nullptr && renderer->m_Scene == &scene)
        {
            renderer->m_PhysicsWorld = &world;
        }
    }
}

std::vector<PhysicsDebugRenderer*>& PhysicsDebugRenderer::Registry()
{
    static std::vector<PhysicsDebugRenderer*> registry;
    return registry;
}

void PhysicsDebugRenderer::EnsureInitialized()
{
    if (m_Material)
    {
        return;
    }

    Ref<Shader> shader = Shader::Create(
        "Raven/Assets/Shaders/Vertex/test.vert",
        "Raven/Assets/Shaders/Fragment/test.frag");
    if (shader == nullptr)
    {
        return;
    }

    PipelineSpecification specification{};
    specification.DebugName = "Physics Debug Pipeline";
    specification.Shader = shader;
    specification.Topology = PrimitiveTopology::Lines;
    specification.Cull = CullMode::None;
    specification.DepthTest = false;
    specification.DepthWrite = false;
    specification.DepthCompare = DepthCompareOperator::LessEqual;
    specification.Blend = true;

    m_Material = CreateRef<Material>(Pipeline::Create(specification));
}

void PhysicsDebugRenderer::UpdateToggleKeys()
{
    auto toggle = [](int key, bool& value, bool& wasPressed)
    {
        const bool pressed = Input::IsKeyPressed(key);
        if (pressed && wasPressed == false)
        {
            value = !value;
        }
        wasPressed = pressed;
    };

    toggle(Key::H, m_Settings.ShowSolverStatistics, m_WasOverlayKeyPressed);
    toggle(Key::B, m_Settings.ShowAABB, m_WasAABBKeyPressed);
    toggle(Key::O, m_Settings.ShowOBB, m_WasOBBKeyPressed);
    toggle(Key::F, m_Settings.ShowFatAABB, m_WasFatAABBKeyPressed);
    toggle(Key::T, m_Settings.ShowDynamicAABBTree, m_WasTreeKeyPressed);
    toggle(Key::P, m_Settings.ShowBroadPhasePairs, m_WasPairKeyPressed);
    toggle(Key::C, m_Settings.ShowContactPoints, m_WasContactPointKeyPressed);
    toggle(Key::N, m_Settings.ShowContactNormals, m_WasContactNormalKeyPressed);
}

void PhysicsDebugRenderer::Render()
{
    UpdateToggleKeys();

    if (m_Scene == nullptr)
    {
        return;
    }

    const bool showWorld = m_Settings.ShowAABB
        || m_Settings.ShowOBB
        || m_Settings.ShowFatAABB
        || m_Settings.ShowDynamicAABBTree
        || m_Settings.ShowBroadPhasePairs
        || m_Settings.ShowContactPoints
        || m_Settings.ShowContactNormals;

    if (showWorld == false && m_Settings.ShowSolverStatistics == false)
    {
        return;
    }

    EnsureInitialized();
    if (m_Material == nullptr)
    {
        return;
    }

    if (showWorld)
    {
        // 3D Debug Shapeは必ず現在のScene Camera Contextを必要とします。
        // BeginScene(Camera)されていないSandbox等では描画せず、前回Cameraの使い回しを防ぎます。
        if (Renderer::GetCameraContext().Valid)
        {
            RenderWorldDebug();
        }
    }

    if (m_Settings.ShowSolverStatistics)
    {
        RenderOverlay();
    }
}

void PhysicsDebugRenderer::RenderWorldDebug()
{
    std::vector<DebugVertex> vertices;
    std::vector<uint32_t> indices;

    if (m_Settings.ShowAABB)
    {
        const math::Vec3 color{ 0.15f, 0.95f, 1.0f };
        for (auto [entity, transform, collider] : m_Scene->View<TransformComponent, ColliderComponent>())
        {
            static_cast<void>(entity);
            AABB bounds{};
            if (ComputeColliderAABB(transform, collider, bounds))
            {
                AddAABB(vertices, indices, bounds, color);
            }
        }
    }

    if (m_Settings.ShowOBB)
    {
        const math::Vec3 color{ 1.0f, 0.25f, 0.85f };
        for (auto [entity, transform, collider] : m_Scene->View<TransformComponent, ColliderComponent>())
        {
            static_cast<void>(entity);
            if (collider.Type != ColliderType::Box)
            {
                continue;
            }

            OBB box{};
            if (ComputeBoxOBB(transform, collider, box))
            {
                AddOBB(vertices, indices, box, color);
            }
        }
    }

    if (m_PhysicsWorld != nullptr)
    {
        if (m_Settings.ShowFatAABB || m_Settings.ShowDynamicAABBTree)
        {
            const DynamicAABBTree& tree = m_PhysicsWorld->GetBroadPhase().GetTree();
            const auto& nodes = tree.GetNodes();
            const bool valid = ValidateDynamicAABBTree(tree).IsValid();

            for (const auto& node : nodes)
            {
                if (node.Height < 0)
                {
                    continue;
                }

                if (node.IsLeaf())
                {
                    if (m_Settings.ShowFatAABB)
                    {
                        const math::Vec3 color = valid
                            ? math::Vec3{ 0.25f, 1.0f, 0.25f }
                            : math::Vec3{ 1.0f, 0.15f, 0.15f };
                        AddAABB(vertices, indices, node.Bounds, color);
                    }
                }
                else if (m_Settings.ShowDynamicAABBTree)
                {
                    const float heightScale = std::min(float(node.Height) / 8.0f, 1.0f);
                    const math::Vec3 color = valid
                        ? math::Vec3{ 0.45f + 0.45f * heightScale, 0.25f, 1.0f - 0.35f * heightScale }
                        : math::Vec3{ 1.0f, 0.15f, 0.15f };
                    AddAABB(vertices, indices, node.Bounds, color);
                }
            }
        }

        if (m_Settings.ShowContactPoints || m_Settings.ShowContactNormals)
        {
            for (const auto& manifold : m_PhysicsWorld->GetContactManifolds())
            {
                for (std::size_t i = 0; i < manifold.PointCount; ++i)
                {
                    const auto& point = manifold.Points[i];
                    if (m_Settings.ShowContactPoints)
                    {
                        AddPointMarker(vertices, indices, point.Position, m_Settings.ContactPointRadius, { 1.0f, 0.2f, 0.2f });
                    }
                    if (m_Settings.ShowContactNormals)
                    {
                        AddLine(vertices, indices, point.Position,
                            point.Position + manifold.Normal * m_Settings.ContactNormalLength,
                            { 1.0f, 1.0f, 0.2f });
                    }
                }
            }
        }

        if (m_Settings.ShowBroadPhasePairs)
        {
            for (const auto& pair : m_PhysicsWorld->GetBroadPhasePairs())
            {
                auto* transformA = m_Scene->TryGetComponent<TransformComponent>(pair.A.GetIndex());
                auto* transformB = m_Scene->TryGetComponent<TransformComponent>(pair.B.GetIndex());
                auto* colliderA = m_Scene->TryGetComponent<ColliderComponent>(pair.A.GetIndex());
                auto* colliderB = m_Scene->TryGetComponent<ColliderComponent>(pair.B.GetIndex());

                if (transformA == nullptr || transformB == nullptr || colliderA == nullptr || colliderB == nullptr)
                {
                    continue;
                }

                AABB aabbA{};
                AABB aabbB{};
                if (ComputeColliderAABB(*transformA, *colliderA, aabbA)
                    && ComputeColliderAABB(*transformB, *colliderB, aabbB))
                {
                    AddLine(vertices, indices, aabbA.GetCenter(), aabbB.GetCenter(), { 1.0f, 0.65f, 0.1f });
                }
            }
        }
    }

    // Renderer::BeginScene(Camera)で設定されたCamera Contextを利用します。
    // Scene View / Game Viewのどちらでも同じDebug Renderer実装を共有できます。
    const RendererCameraContext& cameraContext = Renderer::GetCameraContext();
    SubmitLines(vertices, indices, cameraContext.View, cameraContext.Projection);
}

void PhysicsDebugRenderer::RenderOverlay()
{
    if (m_PhysicsWorld == nullptr)
    {
        return;
    }

    GLint viewport[4] = {};
    glGetIntegerv(GL_VIEWPORT, viewport);
    if (viewport[2] <= 0 || viewport[3] <= 0)
    {
        return;
    }

    std::vector<DebugVertex> vertices;
    std::vector<uint32_t> indices;
    const auto& stats = m_PhysicsWorld->GetSolverDebugStatistics();

    const math::Vec3 titleColor{ 0.25f, 1.0f, 0.85f };
    const math::Vec3 textColor{ 0.92f, 0.92f, 0.92f };
    const math::Vec3 onColor{ 0.35f, 1.0f, 0.35f };
    const math::Vec3 offColor{ 0.65f, 0.65f, 0.65f };

    float y = 16.0f;
    auto addLine = [&](const std::string& text, const math::Vec3& color)
    {
        AddOverlayText(vertices, indices, text, 16.0f, y, 2.0f, viewport[2], viewport[3], color);
        y += 18.0f;
    };

    addLine("PHYSICS DEBUG", titleColor);
    addLine("[H] HIDE OVERLAY", offColor);
    y += 6.0f;
    addLine("SOLVER", titleColor);
    addLine("MANIFOLDS: " + std::to_string(stats.ManifoldCount), textColor);
    addLine("CONTACT POINTS: " + std::to_string(stats.ContactPointCount), textColor);
    addLine("PERSISTENT MANIFOLDS: " + std::to_string(stats.PersistentManifoldCount), textColor);
    addLine("PERSISTENT CONTACTS: " + std::to_string(stats.PersistentContactPointCount), textColor);
    addLine("WARM STARTED: " + std::to_string(stats.WarmStartedConstraintCount), textColor);
    addLine("ITERATIONS: " + std::to_string(stats.VelocityIterations), textColor);
    addLine("MAX PENETRATION: " + FormatFloat(stats.MaxPenetration), textColor);
    addLine("NORMAL IMPULSE: " + FormatFloat(stats.MaxNormalImpulse), textColor);
    addLine("FRICTION IMPULSE: " + FormatFloat(stats.MaxFrictionImpulse), textColor);
    y += 6.0f;
    addLine("VISUALIZATION", titleColor);

    auto addToggleLine = [&](const char* key, const char* label, bool enabled)
    {
        addLine(std::string("[") + key + "] " + label + ": " + OnOff(enabled), enabled ? onColor : offColor);
    };

    addToggleLine("B", "AABB", m_Settings.ShowAABB);
    addToggleLine("O", "OBB", m_Settings.ShowOBB);
    addToggleLine("F", "FAT AABB", m_Settings.ShowFatAABB);
    addToggleLine("T", "DYNAMIC TREE", m_Settings.ShowDynamicAABBTree);
    addToggleLine("P", "BROAD PAIRS", m_Settings.ShowBroadPhasePairs);
    addToggleLine("C", "CONTACT POINT", m_Settings.ShowContactPoints);
    addToggleLine("N", "CONTACT NORMAL", m_Settings.ShowContactNormals);

    SubmitLines(vertices, indices, math::Mat4::Identity(), math::Mat4::Identity());
}

void PhysicsDebugRenderer::SubmitLines(
    std::vector<DebugVertex>& vertices,
    std::vector<uint32_t>& indices,
    const math::Mat4& view,
    const math::Mat4& projection)
{
    if (vertices.empty() || indices.empty())
    {
        return;
    }

    Ref<VertexArray> vertexArray = VertexArray::Create();
    Ref<VertexBuffer> vertexBuffer = VertexBuffer::Create(
        reinterpret_cast<float*>(vertices.data()),
        uint32_t(vertices.size() * sizeof(DebugVertex)));

    vertexBuffer->SetLayout({
        { ShaderDataType::Float3, "a_Position" },
        { ShaderDataType::Float3, "a_Color" },
        { ShaderDataType::Float2, "a_Texcord" }
    });
    Ref<IndexBuffer> indexBuffer = IndexBuffer::Create(indices.data(), uint32_t(indices.size()));
    vertexArray->AddVertexBuffer(vertexBuffer);
    vertexArray->SetIndexBuffer(indexBuffer);

    m_Material->SetUniform("u_View", view);
    m_Material->SetUniform("u_Projection", projection);
    m_Material->SetUniform("u_Tint", math::Vec3{ 1.0f, 1.0f, 1.0f });
    m_Material->SetUniform("u_Alpha", 1.0f);

    // Debug描画も通常Materialと同じRHI経路へ統一します。
    // PipelineのTopology=LinesはMaterial::Bind()でRHICommandListへ設定されるため、
    // DrawIndexedをLegacy RendererAPIへ戻さずRenderCommandから発行することが重要です。
    m_Material->Bind();
    vertexArray->Bind();
    RenderCommand::DrawIndexed(vertexArray, uint32_t(indices.size()));
}

void PhysicsDebugRenderer::AddLine(std::vector<DebugVertex>& vertices, std::vector<uint32_t>& indices,
    const math::Vec3& a, const math::Vec3& b, const math::Vec3& color)
{
    const uint32_t base = uint32_t(vertices.size());
    vertices.push_back({ a, color, {} });
    vertices.push_back({ b, color, {} });
    indices.push_back(base + 0);
    indices.push_back(base + 1);
}

void PhysicsDebugRenderer::AddAABB(std::vector<DebugVertex>& vertices, std::vector<uint32_t>& indices,
    const AABB& bounds, const math::Vec3& color)
{
    const math::Vec3& min = bounds.Min;
    const math::Vec3& max = bounds.Max;
    const math::Vec3 p[8] = {
        {min.x,min.y,min.z},{max.x,min.y,min.z},{max.x,max.y,min.z},{min.x,max.y,min.z},
        {min.x,min.y,max.z},{max.x,min.y,max.z},{max.x,max.y,max.z},{min.x,max.y,max.z}
    };
    const int edges[12][2] = {{0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7}};
    for (const auto& edge : edges)
    {
        AddLine(vertices, indices, p[edge[0]], p[edge[1]], color);
    }
}

void PhysicsDebugRenderer::AddOBB(std::vector<DebugVertex>& vertices, std::vector<uint32_t>& indices,
    const OBB& bounds, const math::Vec3& color)
{
    const math::Vec3 corners[8] = {
        bounds.Center - bounds.Axis[0] * bounds.HalfExtents.x - bounds.Axis[1] * bounds.HalfExtents.y - bounds.Axis[2] * bounds.HalfExtents.z,
        bounds.Center + bounds.Axis[0] * bounds.HalfExtents.x - bounds.Axis[1] * bounds.HalfExtents.y - bounds.Axis[2] * bounds.HalfExtents.z,
        bounds.Center + bounds.Axis[0] * bounds.HalfExtents.x + bounds.Axis[1] * bounds.HalfExtents.y - bounds.Axis[2] * bounds.HalfExtents.z,
        bounds.Center - bounds.Axis[0] * bounds.HalfExtents.x + bounds.Axis[1] * bounds.HalfExtents.y - bounds.Axis[2] * bounds.HalfExtents.z,
        bounds.Center - bounds.Axis[0] * bounds.HalfExtents.x - bounds.Axis[1] * bounds.HalfExtents.y + bounds.Axis[2] * bounds.HalfExtents.z,
        bounds.Center + bounds.Axis[0] * bounds.HalfExtents.x - bounds.Axis[1] * bounds.HalfExtents.y + bounds.Axis[2] * bounds.HalfExtents.z,
        bounds.Center + bounds.Axis[0] * bounds.HalfExtents.x + bounds.Axis[1] * bounds.HalfExtents.y + bounds.Axis[2] * bounds.HalfExtents.z,
        bounds.Center - bounds.Axis[0] * bounds.HalfExtents.x + bounds.Axis[1] * bounds.HalfExtents.y + bounds.Axis[2] * bounds.HalfExtents.z
    };
    const int edges[12][2] = {{0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7}};
    for (const auto& edge : edges)
    {
        AddLine(vertices, indices, corners[edge[0]], corners[edge[1]], color);
    }
}

void PhysicsDebugRenderer::AddPointMarker(std::vector<DebugVertex>& vertices, std::vector<uint32_t>& indices,
    const math::Vec3& center, float radius, const math::Vec3& color)
{
    AddLine(vertices, indices, center - math::Vec3{radius, 0, 0}, center + math::Vec3{radius, 0, 0}, color);
    AddLine(vertices, indices, center - math::Vec3{0, radius, 0}, center + math::Vec3{0, radius, 0}, color);
    AddLine(vertices, indices, center - math::Vec3{0, 0, radius}, center + math::Vec3{0, 0, radius}, color);
}

void PhysicsDebugRenderer::AddOverlayText(
    std::vector<DebugVertex>& vertices,
    std::vector<uint32_t>& indices,
    const std::string& text,
    float x,
    float y,
    float scale,
    int viewportWidth,
    int viewportHeight,
    const math::Vec3& color)
{
    float cursorX = x;
    for (char c : text)
    {
        const uint8_t* rows = GetPhysicsDebugGlyph(c);
        for (int row = 0; row < PhysicsDebugGlyphHeight; ++row)
        {
            for (int col = 0; col < PhysicsDebugGlyphWidth; ++col)
            {
                if ((rows[row] & (1u << (PhysicsDebugGlyphWidth - 1 - col))) == 0)
                {
                    continue;
                }

                const float px0 = cursorX + float(col) * scale;
                const float py0 = y + float(row) * scale;
                const float px1 = px0 + scale;
                const float py1 = py0 + scale;

                const float x0 = (px0 / float(viewportWidth)) * 2.0f - 1.0f;
                const float y0 = 1.0f - (py0 / float(viewportHeight)) * 2.0f;
                const float x1 = (px1 / float(viewportWidth)) * 2.0f - 1.0f;
                const float y1 = 1.0f - (py1 / float(viewportHeight)) * 2.0f;

                const uint32_t base = uint32_t(vertices.size());
                vertices.push_back({ {x0, y0, 0.0f}, color, {} });
                vertices.push_back({ {x1, y0, 0.0f}, color, {} });
                vertices.push_back({ {x1, y1, 0.0f}, color, {} });
                vertices.push_back({ {x0, y1, 0.0f}, color, {} });

                // PipelineはLinesなので各辺を線分として追加します。
                indices.insert(indices.end(), {
                    base + 0, base + 1,
                    base + 1, base + 2,
                    base + 2, base + 3,
                    base + 3, base + 0
                });
            }
        }
        cursorX += float(PhysicsDebugGlyphWidth + 1) * scale;
    }
}

} // namespace Raven::ph
