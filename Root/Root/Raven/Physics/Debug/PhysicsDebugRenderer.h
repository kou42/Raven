#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

#include "Raven/Core/Base.h"
#include "Raven/Core/Input.h"
#include "Raven/Core/KeyCodes.h"
#include "Raven/Math/MathMatrix.h"
#include "Raven/Math/MathVector.h"
#include "Raven/Physics/Collision/AABB.h"
#include "Raven/Physics/Collision/BroadPhase.h"
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

// Broad Phaseの内部状態を既存のLines Pipelineで可視化します。
// B: AABB表示、P: Broad Phase候補ペア表示。
// SceneGameの描画コードへ物理デバッグ処理を混ぜないため、インスタンスは
// Renderer::EndScene()からRenderRegistered()経由で描画されます。
class PhysicsDebugRenderer
{
public:
    PhysicsDebugRenderer(Scene& scene, const math::Mat4& view, const math::Mat4& projection)
        : m_Scene(&scene), m_View(&view), m_Projection(&projection)
    {
        Registry().push_back(this);
    }

    ~PhysicsDebugRenderer()
    {
        auto& registry = Registry();
        registry.erase(std::remove(registry.begin(), registry.end(), this), registry.end());
    }

    PhysicsDebugRenderer(const PhysicsDebugRenderer&) = delete;
    PhysicsDebugRenderer& operator=(const PhysicsDebugRenderer&) = delete;

    static void RenderRegistered()
    {
        for (PhysicsDebugRenderer* renderer : Registry())
        {
            if (renderer != nullptr)
            {
                renderer->Render();
            }
        }
    }

private:
    struct DebugVertex
    {
        math::Vec3 Position{};
        math::Vec3 Color{ 1.0f, 1.0f, 1.0f };
        math::Vec2 Texcoord{};
    };

    static std::vector<PhysicsDebugRenderer*>& Registry()
    {
        static std::vector<PhysicsDebugRenderer*> registry;
        return registry;
    }

    void EnsureInitialized()
    {
        if (m_Material)
        {
            return;
        }

        // SceneGameと同じ基本Shaderを利用します。頂点Colorをそのまま使えるため、
        // デバッグ描画専用Shaderを増やさずLines Pipelineだけを分離できます。
        Ref<Shader> shader = Shader::Create(
            "Raven/Assets/Shaders/Vertex/test.vert",
            "Raven/Assets/Shaders/Fragment/test.frag");
        if (!shader)
        {
            return;
        }

        PipelineSpecification specification{};
        specification.DebugName = "Physics Broad Phase Debug Pipeline";
        specification.Shader = shader;
        specification.Topology = PrimitiveTopology::Lines;
        specification.Cull = CullMode::None;
        specification.DepthTest = true;
        specification.DepthWrite = false;
        specification.DepthCompare = DepthCompareOperator::LessEqual;
        specification.Blend = true;

        m_Material = CreateRef<Material>(Pipeline::Create(specification));
    }

    void UpdateToggleKeys()
    {
        const bool aabbPressed = Input::IsKeyPressed(Key::B);
        if (aabbPressed && !m_WasAABBKeyPressed)
        {
            m_DrawAABBs = !m_DrawAABBs;
        }
        m_WasAABBKeyPressed = aabbPressed;

        const bool pairPressed = Input::IsKeyPressed(Key::P);
        if (pairPressed && !m_WasPairKeyPressed)
        {
            m_DrawPairs = !m_DrawPairs;
        }
        m_WasPairKeyPressed = pairPressed;
    }

    void Render()
    {
        UpdateToggleKeys();
        if ((!m_DrawAABBs && !m_DrawPairs) || m_Scene == nullptr
            || m_View == nullptr || m_Projection == nullptr)
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

        if (m_DrawAABBs)
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

        if (m_DrawPairs)
        {
            std::vector<BroadPhasePair> pairs;
            m_BroadPhase.ComputePairs(*m_Scene, pairs);
            const math::Vec3 color{ 1.0f, 0.65f, 0.10f };

            for (const BroadPhasePair& pair : pairs)
            {
                const TransformComponent* ta = m_Scene->TryGetComponent<TransformComponent>(pair.A.GetIndex());
                const TransformComponent* tb = m_Scene->TryGetComponent<TransformComponent>(pair.B.GetIndex());
                const ColliderComponent* ca = m_Scene->TryGetComponent<ColliderComponent>(pair.A.GetIndex());
                const ColliderComponent* cb = m_Scene->TryGetComponent<ColliderComponent>(pair.B.GetIndex());
                if (!ta || !tb || !ca || !cb)
                {
                    continue;
                }

                AABB a{};
                AABB b{};
                if (!ComputeColliderAABB(*ta, *ca, a) || !ComputeColliderAABB(*tb, *cb, b))
                {
                    continue;
                }

                AddLine(vertices, indices,
                    (a.Min + a.Max) * 0.5f,
                    (b.Min + b.Max) * 0.5f,
                    color);
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
        vao->SetIndexBuffer(IndexBuffer::Create(indices.data(), static_cast<uint32_t>(indices.size())));

        Ref<Mesh> mesh = CreateRef<Mesh>(vao, static_cast<int32_t>(indices.size()));
        m_Material->SetUniform("u_View", *m_View);
        m_Material->SetUniform("u_Projection", *m_Projection);
        m_Material->SetUniform("u_Tint", math::Vec3{ 1.0f, 1.0f, 1.0f });
        m_Material->SetUniform("u_Alpha", 1.0f);
        Renderer::Draw(mesh, m_Material, math::Mat4::Identity());
    }

    static void AddLine(std::vector<DebugVertex>& vertices, std::vector<uint32_t>& indices,
        const math::Vec3& a, const math::Vec3& b, const math::Vec3& color)
    {
        const uint32_t base = static_cast<uint32_t>(vertices.size());
        vertices.push_back({ a, color, {} });
        vertices.push_back({ b, color, {} });
        indices.push_back(base);
        indices.push_back(base + 1);
    }

    static void AddAABB(std::vector<DebugVertex>& vertices, std::vector<uint32_t>& indices,
        const AABB& b, const math::Vec3& color)
    {
        const math::Vec3 c[8] = {
            {b.Min.x,b.Min.y,b.Min.z}, {b.Max.x,b.Min.y,b.Min.z},
            {b.Max.x,b.Min.y,b.Max.z}, {b.Min.x,b.Min.y,b.Max.z},
            {b.Min.x,b.Max.y,b.Min.z}, {b.Max.x,b.Max.y,b.Min.z},
            {b.Max.x,b.Max.y,b.Max.z}, {b.Min.x,b.Max.y,b.Max.z}
        };
        const uint32_t e[12][2] = {
            {0,1},{1,2},{2,3},{3,0}, {4,5},{5,6},{6,7},{7,4},
            {0,4},{1,5},{2,6},{3,7}
        };
        for (const auto& edge : e)
        {
            AddLine(vertices, indices, c[edge[0]], c[edge[1]], color);
        }
    }

private:
    Scene* m_Scene = nullptr;
    const math::Mat4* m_View = nullptr;
    const math::Mat4* m_Projection = nullptr;
    BroadPhase m_BroadPhase;
    Ref<Material> m_Material;
    bool m_DrawAABBs = false;
    bool m_DrawPairs = false;
    bool m_WasAABBKeyPressed = false;
    bool m_WasPairKeyPressed = false;
};

} // namespace Raven::ph
