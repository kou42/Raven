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
#include "Raven/Renderer/Renderer.h"
#include "Raven/Renderer/Shader/Shader.h"
#include "Raven/Scene/Scene.h"

namespace Raven::ph
{
namespace
{
std::string OnOff(bool enabled) { return enabled ? "ON" : "OFF"; }
std::string FormatFloat(float value)
{
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(3) << value;
    return stream.str();
}
} // namespace

PhysicsDebugRenderer::PhysicsDebugRenderer(Scene& scene, const math::Mat4& view, const math::Mat4& projection)
    : m_Scene(&scene), m_View(&view), m_Projection(&projection)
{
    Registry().push_back(this);
}

PhysicsDebugRenderer::~PhysicsDebugRenderer()
{
    auto& registry = Registry();
    registry.erase(std::remove(registry.begin(), registry.end(), this), registry.end());
}

void PhysicsDebugRenderer::RenderRegistered()
{
    for (PhysicsDebugRenderer* renderer : Registry())
        if (renderer) renderer->Render();
}

void PhysicsDebugRenderer::BindPhysicsWorld(Scene& scene, const PhysicsWorld& physicsWorld)
{
    for (PhysicsDebugRenderer* renderer : Registry())
        if (renderer && renderer->m_Scene == &scene) renderer->m_PhysicsWorld = &physicsWorld;
}

std::vector<PhysicsDebugRenderer*>& PhysicsDebugRenderer::Registry()
{
    static std::vector<PhysicsDebugRenderer*> registry;
    return registry;
}

void PhysicsDebugRenderer::EnsureInitialized()
{
    if (m_Material) return;

    Ref<Shader> shader = Shader::Create(
        "Raven/Assets/Shaders/Vertex/test.vert",
        "Raven/Assets/Shaders/Fragment/test.frag");
    if (!shader) return;

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
    // HはOverlayパネルだけを隠します。
    // World-space debugのB/F/T/P/C/N設定は保持されるため、3D可視化だけを残して
    // 画面を広く使いたい場合にも利用できます。
    const bool overlayPressed = Input::IsKeyPressed(Key::H);
    if (overlayPressed && !m_WasOverlayKeyPressed)
        m_Settings.ShowSolverStatistics = !m_Settings.ShowSolverStatistics;
    m_WasOverlayKeyPressed = overlayPressed;

    const bool aabbPressed = Input::IsKeyPressed(Key::B);
    if (aabbPressed && !m_WasAABBKeyPressed) m_Settings.ShowAABB = !m_Settings.ShowAABB;
    m_WasAABBKeyPressed = aabbPressed;

    const bool fatPressed = Input::IsKeyPressed(Key::F);
    if (fatPressed && !m_WasFatAABBKeyPressed) m_Settings.ShowFatAABB = !m_Settings.ShowFatAABB;
    m_WasFatAABBKeyPressed = fatPressed;

    const bool treePressed = Input::IsKeyPressed(Key::T);
    if (treePressed && !m_WasTreeKeyPressed) m_Settings.ShowDynamicAABBTree = !m_Settings.ShowDynamicAABBTree;
    m_WasTreeKeyPressed = treePressed;

    const bool pairPressed = Input::IsKeyPressed(Key::P);
    if (pairPressed && !m_WasPairKeyPressed) m_Settings.ShowBroadPhasePairs = !m_Settings.ShowBroadPhasePairs;
    m_WasPairKeyPressed = pairPressed;

    const bool contactPointPressed = Input::IsKeyPressed(Key::C);
    if (contactPointPressed && !m_WasContactPointKeyPressed) m_Settings.ShowContactPoints = !m_Settings.ShowContactPoints;
    m_WasContactPointKeyPressed = contactPointPressed;

    const bool contactNormalPressed = Input::IsKeyPressed(Key::N);
    if (contactNormalPressed && !m_WasContactNormalKeyPressed) m_Settings.ShowContactNormals = !m_Settings.ShowContactNormals;
    m_WasContactNormalKeyPressed = contactNormalPressed;
}

void PhysicsDebugRenderer::Render()
{
    UpdateToggleKeys();
    if (!m_Scene || !m_View || !m_Projection) return;

    const bool hasWorldDebug = m_Settings.ShowAABB || m_Settings.ShowFatAABB
        || m_Settings.ShowDynamicAABBTree || m_Settings.ShowBroadPhasePairs
        || m_Settings.ShowContactPoints || m_Settings.ShowContactNormals;
    if (!hasWorldDebug && !m_Settings.ShowSolverStatistics) return;

    EnsureInitialized();
    if (!m_Material) return;
    if (hasWorldDebug) RenderWorldDebug();
    if (m_Settings.ShowSolverStatistics) RenderOverlay();
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
            if (ComputeColliderAABB(transform, collider, bounds)) AddAABB(vertices, indices, bounds, color);
        }
    }

    if (m_PhysicsWorld)
    {
        if (m_Settings.ShowFatAABB || m_Settings.ShowDynamicAABBTree)
        {
            const DynamicAABBTree& tree = m_PhysicsWorld->GetBroadPhase().GetTree();
            const auto& nodes = tree.GetNodes();
            const bool treeValid = ValidateDynamicAABBTree(tree).IsValid();
            const math::Vec3 leafColor = treeValid ? math::Vec3{ 0.25f, 1.0f, 0.25f } : math::Vec3{ 1.0f, 0.15f, 0.15f };

            for (const DynamicAABBTreeNode& node : nodes)
            {
                if (node.Height < 0) continue;
                if (node.IsLeaf())
                {
                    if (m_Settings.ShowFatAABB) AddAABB(vertices, indices, node.Bounds, leafColor);
                    continue;
                }
                if (m_Settings.ShowDynamicAABBTree)
                {
                    const float h = std::min(static_cast<float>(node.Height) / 8.0f, 1.0f);
                    const math::Vec3 color = treeValid ? math::Vec3{ 0.45f + 0.45f * h, 0.25f, 1.0f - 0.35f * h }
                                                       : math::Vec3{ 1.0f, 0.15f, 0.15f };
                    AddAABB(vertices, indices, node.Bounds, color);
                }
            }
        }

        if (m_Settings.ShowContactPoints || m_Settings.ShowContactNormals)
        {
            const math::Vec3 pointColor{ 1.0f, 0.2f, 0.2f };
            const math::Vec3 normalColor{ 1.0f, 1.0f, 0.2f };
            for (const ContactManifold& manifold : m_PhysicsWorld->GetContactManifolds())
            {
                for (std::size_t i = 0; i < manifold.PointCount; ++i)
                {
                    const ContactPoint& point = manifold.Points[i];
                    if (m_Settings.ShowContactPoints)
                        AddPointMarker(vertices, indices, point.Position, m_Settings.ContactPointRadius, pointColor);
                    if (m_Settings.ShowContactNormals)
                        AddLine(vertices, indices, point.Position,
                            point.Position + manifold.Normal * m_Settings.ContactNormalLength, normalColor);
                }
            }
        }

        if (m_Settings.ShowBroadPhasePairs)
        {
            const math::Vec3 color{ 1.0f, 0.65f, 0.10f };
            // Simulationが実際に生成したPair Snapshotだけを読み、Debug側ではBroad Phaseを再実行しません。
            for (const BroadPhasePair& pair : m_PhysicsWorld->GetBroadPhasePairs())
            {
                const TransformComponent* ta = m_Scene->TryGetComponent<TransformComponent>(pair.A.GetIndex());
                const TransformComponent* tb = m_Scene->TryGetComponent<TransformComponent>(pair.B.GetIndex());
                const ColliderComponent* ca = m_Scene->TryGetComponent<ColliderComponent>(pair.A.GetIndex());
                const ColliderComponent* cb = m_Scene->TryGetComponent<ColliderComponent>(pair.B.GetIndex());
                if (!ta || !tb || !ca || !cb) continue;

                AABB a{}, b{};
                if (ComputeColliderAABB(*ta, *ca, a) && ComputeColliderAABB(*tb, *cb, b))
                    AddLine(vertices, indices, a.GetCenter(), b.GetCenter(), color);
            }
        }
    }

    SubmitLines(vertices, indices, *m_View, *m_Projection);
}

void PhysicsDebugRenderer::RenderOverlay()
{
    if (!m_PhysicsWorld) return;

    GLint viewport[4] = {};
    glGetIntegerv(GL_VIEWPORT, viewport);
    const int viewportWidth = viewport[2];
    const int viewportHeight = viewport[3];
    if (viewportWidth <= 0 || viewportHeight <= 0) return;

    std::vector<DebugVertex> vertices;
    std::vector<uint32_t> indices;
    const PhysicsSolverDebugStatistics& stats = m_PhysicsWorld->GetSolverDebugStatistics();

    const math::Vec3 titleColor{ 0.25f, 1.0f, 0.85f };
    const math::Vec3 textColor{ 0.92f, 0.92f, 0.92f };
    const math::Vec3 enabledColor{ 0.35f, 1.0f, 0.35f };
    const math::Vec3 disabledColor{ 0.65f, 0.65f, 0.65f };
    constexpr float left = 16.0f, top = 16.0f, scale = 2.0f, lineHeight = 18.0f;

    float y = top;
    auto addText = [&](const std::string& text, const math::Vec3& color)
    {
        AddOverlayText(vertices, indices, text, left, y, scale, viewportWidth, viewportHeight, color);
        y += lineHeight;
    };

    addText("PHYSICS DEBUG", titleColor);
    addText("[H] HIDE OVERLAY", disabledColor);
    y += 6.0f;
    addText("SOLVER", titleColor);
    addText("MANIFOLDS: " + std::to_string(stats.ManifoldCount), textColor);
    addText("CONTACT POINTS: " + std::to_string(stats.ContactPointCount), textColor);
    addText("PERSISTENT MANIFOLDS: " + std::to_string(stats.PersistentManifoldCount), textColor);
    addText("PERSISTENT CONTACTS: " + std::to_string(stats.PersistentContactPointCount), textColor);
    addText("WARM STARTED: " + std::to_string(stats.WarmStartedConstraintCount), textColor);
    addText("ITERATIONS: " + std::to_string(stats.VelocityIterations), textColor);
    addText("MAX PENETRATION: " + FormatFloat(stats.MaxPenetration), textColor);
    addText("NORMAL IMPULSE: " + FormatFloat(stats.MaxNormalImpulse), textColor);
    addText("FRICTION IMPULSE: " + FormatFloat(stats.MaxFrictionImpulse), textColor);
    y += 6.0f;
    addText("VISUALIZATION", titleColor);

    auto addToggle = [&](const char* key, const char* label, bool enabled)
    {
        addText(std::string("[") + key + "] " + label + ": " + OnOff(enabled),
            enabled ? enabledColor : disabledColor);
    };
    addToggle("B", "AABB", m_Settings.ShowAABB);
    addToggle("F", "FAT AABB", m_Settings.ShowFatAABB);
    addToggle("T", "DYNAMIC TREE", m_Settings.ShowDynamicAABBTree);
    addToggle("P", "BROAD PAIRS", m_Settings.ShowBroadPhasePairs);
    addToggle("C", "CONTACT POINT", m_Settings.ShowContactPoints);
    addToggle("N", "CONTACT NORMAL", m_Settings.ShowContactNormals);

    // Overlay頂点はNDC座標なのでCamera行列を通しません。
    SubmitLines(vertices, indices, math::Mat4::Identity(), math::Mat4::Identity());
}

void PhysicsDebugRenderer::SubmitLines(
    std::vector<DebugVertex>& vertices,
    std::vector<uint32_t>& indices,
    const math::Mat4& view,
    const math::Mat4& projection)
{
    if (vertices.empty() || indices.empty()) return;

    Ref<VertexArray> vao = VertexArray::Create();
    Ref<VertexBuffer> vbo = VertexBuffer::Create(
        reinterpret_cast<float*>(vertices.data()),
        static_cast<uint32_t>(vertices.size() * sizeof(DebugVertex)));
    vbo->SetLayout({
        { ShaderDataType::Float3, "a_Position" },
        { ShaderDataType::Float3, "a_Color" },
        { ShaderDataType::Float2, "a_Texcord" }
    });
    vao->AddVertexBuffer(vbo);
    vao->SetIndexBuffer(IndexBuffer::Create(indices.data(), static_cast<uint32_t>(indices.size())));

    Ref<Mesh> mesh = CreateRef<Mesh>(vao, static_cast<int32_t>(indices.size()));
    m_Material->SetUniform("u_View", view);
    m_Material->SetUniform("u_Projection", projection);
    m_Material->SetUniform("u_Tint", math::Vec3{ 1.0f, 1.0f, 1.0f });
    m_Material->SetUniform("u_Alpha", 1.0f);
    Renderer::Draw(mesh, m_Material, math::Mat4::Identity());
}

void PhysicsDebugRenderer::AddLine(std::vector<DebugVertex>& vertices, std::vector<uint32_t>& indices,
    const math::Vec3& a, const math::Vec3& b, const math::Vec3& color)
{
    const uint32_t base = static_cast<uint32_t>(vertices.size());
    vertices.push_back({ a, color, {} });
    vertices.push_back({ b, color, {} });
    indices.push_back(base);
    indices.push_back(base + 1);
}

void PhysicsDebugRenderer::AddAABB(std::vector<DebugVertex>& vertices, std::vector<uint32_t>& indices,
    const AABB& bounds, const math::Vec3& color)
{
    const math::Vec3 c[8] = {
        {bounds.Min.x,bounds.Min.y,bounds.Min.z},{bounds.Max.x,bounds.Min.y,bounds.Min.z},
        {bounds.Max.x,bounds.Min.y,bounds.Max.z},{bounds.Min.x,bounds.Min.y,bounds.Max.z},
        {bounds.Min.x,bounds.Max.y,bounds.Min.z},{bounds.Max.x,bounds.Max.y,bounds.Min.z},
        {bounds.Max.x,bounds.Max.y,bounds.Max.z},{bounds.Min.x,bounds.Max.y,bounds.Max.z}
    };
    const uint32_t e[12][2] = {{0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7}};
    for (const auto& edge : e) AddLine(vertices, indices, c[edge[0]], c[edge[1]], color);
}

void PhysicsDebugRenderer::AddPointMarker(std::vector<DebugVertex>& vertices, std::vector<uint32_t>& indices,
    const math::Vec3& position, float radius, const math::Vec3& color)
{
    const float r = std::max(radius, 0.0f);
    const math::Vec3 x{ r,0,0 }, y{ 0,r,0 }, z{ 0,0,r };
    AddLine(vertices, indices, position - x, position + x, color);
    AddLine(vertices, indices, position - y, position + y, color);
    AddLine(vertices, indices, position - z, position + z, color);
}

void PhysicsDebugRenderer::AddOverlayText(std::vector<DebugVertex>& vertices, std::vector<uint32_t>& indices,
    const std::string& text, float pixelX, float pixelY, float pixelScale,
    int viewportWidth, int viewportHeight, const math::Vec3& color)
{
    if (viewportWidth <= 0 || viewportHeight <= 0 || pixelScale <= 0.0f) return;
    auto toNdc = [&](float x, float y)
    {
        return math::Vec3{ (x / static_cast<float>(viewportWidth)) * 2.0f - 1.0f,
            1.0f - (y / static_cast<float>(viewportHeight)) * 2.0f, 0.0f };
    };

    float cursorX = pixelX;
    for (char c : text)
    {
        const detail::Glyph glyph = detail::GetPhysicsDebugGlyph(c);
        // 連続したbitを1本の横線へまとめ、Overlayの頂点数を抑えます。
        for (int row = 0; row < 7; ++row)
        {
            int column = 0;
            while (column < 5)
            {
                const uint8_t mask = static_cast<uint8_t>(1u << (4 - column));
                if ((glyph[row] & mask) == 0) { ++column; continue; }
                const int runStart = column;
                while (column < 5)
                {
                    const uint8_t runMask = static_cast<uint8_t>(1u << (4 - column));
                    if ((glyph[row] & runMask) == 0) break;
                    ++column;
                }
                const float x0 = cursorX + static_cast<float>(runStart) * pixelScale;
                const float x1 = cursorX + static_cast<float>(column) * pixelScale;
                const float y = pixelY + static_cast<float>(row) * pixelScale;
                AddLine(vertices, indices, toNdc(x0, y), toNdc(x1, y), color);
            }
        }
        cursorX += 6.0f * pixelScale;
    }
}

} // namespace Raven::ph
