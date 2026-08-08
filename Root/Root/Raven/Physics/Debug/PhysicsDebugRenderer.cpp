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
// Debug Overlay用の文字列整形ヘルパー。
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

PhysicsDebugRenderer::PhysicsDebugRenderer(
    Scene& scene,
    const math::Mat4& view,
    const math::Mat4& projection)
    : m_Scene(&scene)
    , m_View(&view)
    , m_Projection(&projection)
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
    // 同一Sceneに紐づくデバッグレンダラを一括描画します。
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
    // 同一Sceneに属するレンダラだけへPhysicsWorld参照を配布します。
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
    // デバッグ線は視認優先で、カリング無効・深度書き込み無効に設定します。
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
    // 押下エッジでだけトグルし、押しっぱなしで連続反転しないようにします。
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
    // World可視化とOverlayは独立しており、どちらか有効なら描画を実行します。
    UpdateToggleKeys();

    if (m_Scene == nullptr || m_View == nullptr || m_Projection == nullptr)
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
        RenderWorldDebug();
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
        // Broad Phaseの入力となるAABBを可視化します。
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
        // Narrow Phaseで使う回転Box境界を可視化します。
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
            // Treeの健全性に応じて色を変え、異常ノードを即座に識別しやすくします。
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
            // マニホールドの接点と法線を描画し、接触安定性をその場で確認できるようにします。
            for (const auto& manifold : m_PhysicsWorld->GetContactManifolds())
            {
                for (std::size_t i = 0; i < manifold.PointCount; ++i)
                {
                    const auto& point = manifold.Points[i];

                    if (m_Settings.ShowContactPoints)
                    {
                        AddPointMarker(
                            vertices,
                            indices,
                            point.Position,
                            m_Settings.ContactPointRadius,
                            { 1.0f, 0.2f, 0.2f });
                    }

                    if (m_Settings.ShowContactNormals)
                    {
                        AddLine(
                            vertices,
                            indices,
                            point.Position,
                            point.Position + manifold.Normal * m_Settings.ContactNormalLength,
                            { 1.0f, 1.0f, 0.2f });
                    }
                }
            }
        }

        if (m_Settings.ShowBroadPhasePairs)
        {
            // Broad Phaseで選ばれたペア中心を線で結び、過剰候補を視覚確認します。
            for (const auto& pair : m_PhysicsWorld->GetBroadPhasePairs())
            {
                auto* transformA = m_Scene->TryGetComponent<TransformComponent>(pair.A.GetIndex());
                auto* transformB = m_Scene->TryGetComponent<TransformComponent>(pair.B.GetIndex());
                auto* colliderA = m_Scene->TryGetComponent<ColliderComponent>(pair.A.GetIndex());
                auto* colliderB = m_Scene->TryGetComponent<ColliderComponent>(pair.B.GetIndex());

                if (transformA == nullptr
                    || transformB == nullptr
                    || colliderA == nullptr
                    || colliderB == nullptr)
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

    SubmitLines(vertices, indices, *m_View, *m_Projection);
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
    // OverlayはSolver統計とトグル状態を同時表示し、調査サイクルを短縮します。

    const math::Vec3 titleColor{ 0.25f, 1.0f, 0.85f };
    const math::Vec3 textColor{ 0.92f, 0.92f, 0.92f };
    const math::Vec3 onColor{ 0.35f, 1.0f, 0.35f };
    const math::Vec3 offColor{ 0.65f, 0.65f, 0.65f };

    float y = 16.0f;
    auto addLine = [&](const std::string& text, const math::Vec3& color)
    {
        // NDC変換はAddOverlayText内で行うため、ここではピクセル基準で行送りします。
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
        addLine(
            std::string("[") + key + "] " + label + ": " + OnOff(enabled),
            enabled ? onColor : offColor);
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
    // 毎フレームのデバッグ描画は一時VAO/VBOで組み立て、即時送信します。
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

    vertexArray->AddVertexBuffer(vertexBuffer);
    vertexArray->SetIndexBuffer(IndexBuffer::Create(indices.data(), uint32_t(indices.size())));

    Ref<Mesh> mesh = CreateRef<Mesh>(vertexArray, int32_t(indices.size()));

    m_Material->SetUniform("u_View", view);
    m_Material->SetUniform("u_Projection", projection);
    m_Material->SetUniform("u_Tint", math::Vec3{ 1.0f, 1.0f, 1.0f });
    m_Material->SetUniform("u_Alpha", 1.0f);

    Renderer::Draw(mesh, m_Material, math::Mat4::Identity());
}

void PhysicsDebugRenderer::AddLine(
    std::vector<DebugVertex>& vertices,
    std::vector<uint32_t>& indices,
    const math::Vec3& start,
    const math::Vec3& end,
    const math::Vec3& color)
{
    // LineList前提で2頂点+2indexを追加します。
    const uint32_t base = uint32_t(vertices.size());
    vertices.push_back({ start, color, {} });
    vertices.push_back({ end, color, {} });
    indices.push_back(base);
    indices.push_back(base + 1);
}

void PhysicsDebugRenderer::AddAABB(
    std::vector<DebugVertex>& vertices,
    std::vector<uint32_t>& indices,
    const AABB& bounds,
    const math::Vec3& color)
{
    // AABBの12本エッジを明示し、軸整列境界をワイヤ表示します。
    const math::Vec3 corners[8] = {
        { bounds.Min.x, bounds.Min.y, bounds.Min.z },
        { bounds.Max.x, bounds.Min.y, bounds.Min.z },
        { bounds.Max.x, bounds.Min.y, bounds.Max.z },
        { bounds.Min.x, bounds.Min.y, bounds.Max.z },
        { bounds.Min.x, bounds.Max.y, bounds.Min.z },
        { bounds.Max.x, bounds.Max.y, bounds.Min.z },
        { bounds.Max.x, bounds.Max.y, bounds.Max.z },
        { bounds.Min.x, bounds.Max.y, bounds.Max.z }
    };

    const uint32_t edges[12][2] = {
        { 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 0 },
        { 4, 5 }, { 5, 6 }, { 6, 7 }, { 7, 4 },
        { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 }
    };

    for (const auto& edge : edges)
    {
        AddLine(vertices, indices, corners[edge[0]], corners[edge[1]], color);
    }
}

void PhysicsDebugRenderer::AddOBB(
    std::vector<DebugVertex>& vertices,
    std::vector<uint32_t>& indices,
    const OBB& box,
    const math::Vec3& color)
{
    // 8頂点を符号組み合わせで生成し、OBBのワイヤフレームを構築します。
    math::Vec3 corners[8];
    int index = 0;

    for (int y = -1; y <= 1; y += 2)
    {
        for (int z = -1; z <= 1; z += 2)
        {
            for (int x = -1; x <= 1; x += 2)
            {
                corners[index++] = box.Center
                    + box.Axis[0] * (box.HalfExtents.x * float(x))
                    + box.Axis[1] * (box.HalfExtents.y * float(y))
                    + box.Axis[2] * (box.HalfExtents.z * float(z));
            }
        }
    }

    const uint32_t edges[12][2] = {
        { 0, 1 }, { 0, 2 }, { 0, 4 }, { 1, 3 },
        { 1, 5 }, { 2, 3 }, { 2, 6 }, { 3, 7 },
        { 4, 5 }, { 4, 6 }, { 5, 7 }, { 6, 7 }
    };

    for (const auto& edge : edges)
    {
        AddLine(vertices, indices, corners[edge[0]], corners[edge[1]], color);
    }
}

void PhysicsDebugRenderer::AddPointMarker(
    std::vector<DebugVertex>& vertices,
    std::vector<uint32_t>& indices,
    const math::Vec3& point,
    float radius,
    const math::Vec3& color)
{
    // 接触点は小さな3軸クロスマーカーで視認性を確保します。
    const float r = std::max(radius, 0.0f);

    AddLine(vertices, indices, point - math::Vec3{ r, 0.0f, 0.0f }, point + math::Vec3{ r, 0.0f, 0.0f }, color);
    AddLine(vertices, indices, point - math::Vec3{ 0.0f, r, 0.0f }, point + math::Vec3{ 0.0f, r, 0.0f }, color);
    AddLine(vertices, indices, point - math::Vec3{ 0.0f, 0.0f, r }, point + math::Vec3{ 0.0f, 0.0f, r }, color);
}

void PhysicsDebugRenderer::AddOverlayText(
    std::vector<DebugVertex>& vertices,
    std::vector<uint32_t>& indices,
    const std::string& text,
    float px,
    float py,
    float scale,
    int width,
    int height,
    const math::Vec3& color)
{
    // 5x7グリフを横線セグメントへ展開し、フォントテクスチャ無しで描画します。
    if (width <= 0 || height <= 0 || scale <= 0.0f)
    {
        return;
    }

    auto toNdc = [&](float x, float y)
    {
        return math::Vec3{
            (x / float(width)) * 2.0f - 1.0f,
            1.0f - (y / float(height)) * 2.0f,
            0.0f
        };
    };

    float cursor = px;
    for (char ch : text)
    {
        const auto glyph = detail::GetPhysicsDebugGlyph(ch);

        for (int row = 0; row < 7; ++row)
        {
            int col = 0;
            while (col < 5)
            {
                const uint8_t mask = uint8_t(1u << (4 - col));
                if ((glyph[row] & mask) == 0)
                {
                    ++col;
                    continue;
                }

                const int start = col;
                while (col < 5 && (glyph[row] & uint8_t(1u << (4 - col))))
                {
                    ++col;
                }

                AddLine(
                    vertices,
                    indices,
                    toNdc(cursor + float(start) * scale, py + float(row) * scale),
                    toNdc(cursor + float(col) * scale, py + float(row) * scale),
                    color);
            }
        }

        cursor += 6.0f * scale;
    }
}

} // namespace Raven::ph
