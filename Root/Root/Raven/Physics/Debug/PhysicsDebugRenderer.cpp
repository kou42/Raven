#include <algorithm>
#include <cstdint>
#include <vector>

#include "Raven/Core/Input.h"
#include "Raven/Core/KeyCodes.h"

#include "Raven/Physics/Collision/DynamicAABBTreeValidation.h"
#include "Raven/Physics/Debug/PhysicsDebugRenderer.h"
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

PhysicsDebugRenderer::PhysicsDebugRenderer(
    Scene& scene,
    const math::Mat4& view,
    const math::Mat4& projection)
{
    m_Scene = &scene;
    m_View = &view;
    m_Projection = &projection;
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
    {
        if (renderer != nullptr)
        {
            renderer->Render();
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

    if (!shader)
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
    // Keyboardと将来のDebug UIは、同じPhysicsDebugSettingsを書き換えます。
    // 表示状態の所有場所を1つにすることで、UI導入時にRenderer内部boolとの同期が
    // 必要にならないようにしています。
    const bool aabbPressed = Input::IsKeyPressed(Key::B);
    if (aabbPressed && !m_WasAABBKeyPressed)
    {
        m_Settings.ShowAABB = !m_Settings.ShowAABB;
    }
    m_WasAABBKeyPressed = aabbPressed;

    const bool fatPressed = Input::IsKeyPressed(Key::F);
    if (fatPressed && !m_WasFatAABBKeyPressed)
    {
        m_Settings.ShowFatAABB = !m_Settings.ShowFatAABB;
    }
    m_WasFatAABBKeyPressed = fatPressed;

    const bool treePressed = Input::IsKeyPressed(Key::T);
    if (treePressed && !m_WasTreeKeyPressed)
    {
        m_Settings.ShowDynamicAABBTree = !m_Settings.ShowDynamicAABBTree;
    }
    m_WasTreeKeyPressed = treePressed;

    const bool pairPressed = Input::IsKeyPressed(Key::P);
    if (pairPressed && !m_WasPairKeyPressed)
    {
        m_Settings.ShowBroadPhasePairs = !m_Settings.ShowBroadPhasePairs;
    }
    m_WasPairKeyPressed = pairPressed;
}

void PhysicsDebugRenderer::Render()
{
    UpdateToggleKeys();

    if (m_Scene == nullptr || m_View == nullptr || m_Projection == nullptr)
    {
        return;
    }

    if (!m_Settings.ShowAABB
        && !m_Settings.ShowFatAABB
        && !m_Settings.ShowDynamicAABBTree
        && !m_Settings.ShowBroadPhasePairs)
    {
        return;
    }

    EnsureInitialized();
    if (!m_Material)
    {
        return;
    }

    std::vector<DebugVertex> vertices;
    std::vector<uint32_t> indices;

    std::vector<BroadPhasePair> pairs;
    if (m_Settings.ShowFatAABB
        || m_Settings.ShowDynamicAABBTree
        || m_Settings.ShowBroadPhasePairs)
    {
        // 現段階では既存実装を維持してDebug用BroadPhaseを同期します。
        // 次段階ではPhysicsWorld::GetBroadPhase()の実Tree参照へ切り替えます。
        m_BroadPhase.ComputePairs(*m_Scene, pairs);
    }

    if (m_Settings.ShowAABB)
    {
        const math::Vec3 color{ 0.15f, 0.95f, 1.0f };
        for (auto [entity, transform, collider]
            : m_Scene->View<TransformComponent, ColliderComponent>())
        {
            static_cast<void>(entity);
            AABB bounds{};
            if (ComputeColliderAABB(transform, collider, bounds))
            {
                AddAABB(vertices, indices, bounds, color);
            }
        }
    }

    if (m_Settings.ShowFatAABB || m_Settings.ShowDynamicAABBTree)
    {
        const DynamicAABBTree& tree = m_BroadPhase.GetTree();
        const auto& nodes = tree.GetNodes();

        const DynamicAABBTreeValidationResult validation = ValidateDynamicAABBTree(tree);
        const bool treeValid = validation.IsValid();

        const math::Vec3 leafColor = treeValid
            ? math::Vec3{ 0.25f, 1.0f, 0.25f }
            : math::Vec3{ 1.0f, 0.15f, 0.15f };

        for (uint32_t nodeId = 0;
            nodeId < static_cast<uint32_t>(nodes.size());
            ++nodeId)
        {
            const DynamicAABBTreeNode& node = nodes[nodeId];
            if (node.Height < 0)
            {
                continue;
            }

            if (node.IsLeaf())
            {
                if (m_Settings.ShowFatAABB)
                {
                    AddAABB(vertices, indices, node.Bounds, leafColor);
                }
                continue;
            }

            if (m_Settings.ShowDynamicAABBTree)
            {
                const float heightFactor =
                    std::min(static_cast<float>(node.Height) / 8.0f, 1.0f);
                const math::Vec3 branchColor = treeValid
                    ? math::Vec3{
                        0.45f + 0.45f * heightFactor,
                        0.25f,
                        1.0f - 0.35f * heightFactor }
                    : math::Vec3{ 1.0f, 0.15f, 0.15f };

                AddAABB(vertices, indices, node.Bounds, branchColor);
            }
        }
    }

    if (m_Settings.ShowBroadPhasePairs)
    {
        const math::Vec3 color{ 1.0f, 0.65f, 0.10f };

        for (const BroadPhasePair& pair : pairs)
        {
            const TransformComponent* ta =
                m_Scene->TryGetComponent<TransformComponent>(pair.A.GetIndex());
            const TransformComponent* tb =
                m_Scene->TryGetComponent<TransformComponent>(pair.B.GetIndex());
            const ColliderComponent* ca =
                m_Scene->TryGetComponent<ColliderComponent>(pair.A.GetIndex());
            const ColliderComponent* cb =
                m_Scene->TryGetComponent<ColliderComponent>(pair.B.GetIndex());

            if (!ta || !tb || !ca || !cb)
            {
                continue;
            }

            AABB a{};
            AABB b{};
            if (!ComputeColliderAABB(*ta, *ca, a)
                || !ComputeColliderAABB(*tb, *cb, b))
            {
                continue;
            }

            AddLine(vertices, indices, a.GetCenter(), b.GetCenter(), color);
        }
    }

    if (indices.empty())
    {
        return;
    }

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
    vao->SetIndexBuffer(IndexBuffer::Create(
        indices.data(),
        static_cast<uint32_t>(indices.size())));

    Ref<Mesh> mesh = CreateRef<Mesh>(vao, static_cast<int32_t>(indices.size()));
    m_Material->SetUniform("u_View", *m_View);
    m_Material->SetUniform("u_Projection", *m_Projection);
    m_Material->SetUniform("u_Tint", math::Vec3{ 1.0f, 1.0f, 1.0f });
    m_Material->SetUniform("u_Alpha", 1.0f);
    Renderer::Draw(mesh, m_Material, math::Mat4::Identity());
}

void PhysicsDebugRenderer::AddLine(
    std::vector<DebugVertex>& vertices,
    std::vector<uint32_t>& indices,
    const math::Vec3& a,
    const math::Vec3& b,
    const math::Vec3& color)
{
    const uint32_t base = static_cast<uint32_t>(vertices.size());
    vertices.push_back({ a, color, {} });
    vertices.push_back({ b, color, {} });
    indices.push_back(base);
    indices.push_back(base + 1);
}

void PhysicsDebugRenderer::AddAABB(
    std::vector<DebugVertex>& vertices,
    std::vector<uint32_t>& indices,
    const AABB& bounds,
    const math::Vec3& color)
{
    const math::Vec3 c[8] = {
        {bounds.Min.x, bounds.Min.y, bounds.Min.z},
        {bounds.Max.x, bounds.Min.y, bounds.Min.z},
        {bounds.Max.x, bounds.Min.y, bounds.Max.z},
        {bounds.Min.x, bounds.Min.y, bounds.Max.z},
        {bounds.Min.x, bounds.Max.y, bounds.Min.z},
        {bounds.Max.x, bounds.Max.y, bounds.Min.z},
        {bounds.Max.x, bounds.Max.y, bounds.Max.z},
        {bounds.Min.x, bounds.Max.y, bounds.Max.z}
    };

    const uint32_t e[12][2] = {
        {0,1},{1,2},{2,3},{3,0},
        {4,5},{5,6},{6,7},{7,4},
        {0,4},{1,5},{2,6},{3,7}
    };

    for (const auto& edge : e)
    {
        AddLine(vertices, indices, c[edge[0]], c[edge[1]], color);
    }
}

} // namespace Raven::ph
