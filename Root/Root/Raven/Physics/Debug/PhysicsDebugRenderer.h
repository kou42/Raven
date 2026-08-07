#pragma once

#include <cstdint>
#include <vector>

#include "Raven/Core/Base.h"
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

// ============================================================================
// PhysicsDebugRenderer
// ============================================================================
// Broad Phaseの内部状態を、既存RendererのLines Pipelineを使って可視化します。
//
// 表示内容
//   1. Sphere / Box Colliderから生成されたAABBの12辺
//   2. Broad PhaseがNarrow Phase候補として残したEntityペア間の線
//
// 重要:
// このクラスは「物理結果を変更しない」読み取り専用のデバッグ描画です。
// AABBや候補ペアをSceneから再計算しますが、Collider / Transform / RigidBodyへ
// 書き込みは行いません。
class PhysicsDebugRenderer
{
public:
    void Initialize(const Ref<Shader>& shader)
    {
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
        m_Material->SetUniform("u_Tint", math::Vec3{ 1.0f, 1.0f, 1.0f });
        m_Material->SetUniform("u_Alpha", 1.0f);
    }

    void Shutdown()
    {
        m_Material.reset();
    }

    void Render(
        Scene& scene,
        const math::Mat4& view,
        const math::Mat4& projection,
        bool drawAABBs,
        bool drawCandidatePairs)
    {
        if (!m_Material || (!drawAABBs && !drawCandidatePairs))
        {
            return;
        }

        std::vector<DebugVertex> vertices;
        std::vector<uint32_t> indices;

        // --------------------------------------------------------------------
        // AABB表示
        // --------------------------------------------------------------------
        // Broad Phaseが実際に使うComputeColliderAABB()をそのまま呼び出します。
        // そのため「デバッグ用に別計算した箱」と「Broad Phaseで使う箱」が
        // 食い違うことを防げます。
        if (drawAABBs)
        {
            const math::Vec3 aabbColor{ 0.15f, 0.95f, 1.0f };

            for (auto [entity, transform, collider]
                : scene.View<TransformComponent, ColliderComponent>())
            {
                static_cast<void>(entity);

                AABB bounds{};
                if (!ComputeColliderAABB(transform, collider, bounds))
                {
                    // 無限Planeなど有限AABBを持たないColliderは描画しません。
                    continue;
                }

                AddAABB(vertices, indices, bounds, aabbColor);
            }
        }

        // --------------------------------------------------------------------
        // Broad Phase候補ペア表示
        // --------------------------------------------------------------------
        // 候補ペアの各ColliderのAABB中心を線で結びます。
        // この線が存在する組だけが、このBroad Phaseを通過してNarrow Phaseへ
        // 渡される可能性があります。
        if (drawCandidatePairs)
        {
            std::vector<BroadPhasePair> pairs;
            m_BroadPhase.ComputePairs(scene, pairs);

            const math::Vec3 pairColor{ 1.0f, 0.65f, 0.10f };

            for (const BroadPhasePair& pair : pairs)
            {
                if (!scene.IsEntityAlive(pair.A) || !scene.IsEntityAlive(pair.B))
                {
                    continue;
                }

                const TransformComponent* transformA =
                    scene.TryGetComponent<TransformComponent>(pair.A.GetIndex());
                const TransformComponent* transformB =
                    scene.TryGetComponent<TransformComponent>(pair.B.GetIndex());
                const ColliderComponent* colliderA =
                    scene.TryGetComponent<ColliderComponent>(pair.A.GetIndex());
                const ColliderComponent* colliderB =
                    scene.TryGetComponent<ColliderComponent>(pair.B.GetIndex());

                if (transformA == nullptr || transformB == nullptr
                    || colliderA == nullptr || colliderB == nullptr)
                {
                    continue;
                }

                AABB boundsA{};
                AABB boundsB{};
                if (!ComputeColliderAABB(*transformA, *colliderA, boundsA)
                    || !ComputeColliderAABB(*transformB, *colliderB, boundsB))
                {
                    continue;
                }

                const math::Vec3 centerA = (boundsA.Min + boundsA.Max) * 0.5f;
                const math::Vec3 centerB = (boundsB.Min + boundsB.Max) * 0.5f;
                AddLine(vertices, indices, centerA, centerB, pairColor);
            }
        }

        if (vertices.empty() || indices.empty())
        {
            return;
        }

        // VertexBufferに動的更新APIがまだないため、現段階ではデバッグ描画時だけ
        // 1フレーム分のLine Meshを再構築します。
        // 将来デバッグ描画量が増えた段階でDynamic VertexBufferへ置き換えられます。
        Ref<VertexArray> vertexArray = VertexArray::Create();
        Ref<VertexBuffer> vertexBuffer = VertexBuffer::Create(
            reinterpret_cast<float*>(vertices.data()),
            static_cast<uint32_t>(vertices.size() * sizeof(DebugVertex)));

        vertexBuffer->SetLayout({
            { ShaderDataType::Float3, "a_Position" },
            { ShaderDataType::Float3, "a_Color" },
            { ShaderDataType::Float2, "a_Texcord" }
        });

        Ref<IndexBuffer> indexBuffer = IndexBuffer::Create(
            indices.data(),
            static_cast<uint32_t>(indices.size()));

        vertexArray->AddVertexBuffer(vertexBuffer);
        vertexArray->SetIndexBuffer(indexBuffer);

        Ref<Mesh> mesh = CreateRef<Mesh>(
            vertexArray,
            static_cast<int32_t>(indices.size()));

        m_Material->SetUniform("u_View", view);
        m_Material->SetUniform("u_Projection", projection);
        m_Material->SetUniform("u_Tint", math::Vec3{ 1.0f, 1.0f, 1.0f });
        m_Material->SetUniform("u_Alpha", 1.0f);

        Renderer::Draw(mesh, m_Material, math::Mat4::Identity());
    }

private:
    struct DebugVertex
    {
        math::Vec3 Position{};
        math::Vec3 Color{ 1.0f, 1.0f, 1.0f };
        math::Vec2 Texcoord{};
    };

    static void AddLine(
        std::vector<DebugVertex>& vertices,
        std::vector<uint32_t>& indices,
        const math::Vec3& a,
        const math::Vec3& b,
        const math::Vec3& color)
    {
        const uint32_t baseIndex = static_cast<uint32_t>(vertices.size());
        vertices.push_back(DebugVertex{ a, color, math::Vec2{} });
        vertices.push_back(DebugVertex{ b, color, math::Vec2{} });
        indices.push_back(baseIndex);
        indices.push_back(baseIndex + 1);
    }

    static void AddAABB(
        std::vector<DebugVertex>& vertices,
        std::vector<uint32_t>& indices,
        const AABB& bounds,
        const math::Vec3& color)
    {
        // AABBの8頂点。
        // 下側4点を0-3、上側4点を4-7としておくと、12辺を規則的に接続できます。
        const math::Vec3 corners[8] =
        {
            { bounds.Min.x, bounds.Min.y, bounds.Min.z },
            { bounds.Max.x, bounds.Min.y, bounds.Min.z },
            { bounds.Max.x, bounds.Min.y, bounds.Max.z },
            { bounds.Min.x, bounds.Min.y, bounds.Max.z },
            { bounds.Min.x, bounds.Max.y, bounds.Min.z },
            { bounds.Max.x, bounds.Max.y, bounds.Min.z },
            { bounds.Max.x, bounds.Max.y, bounds.Max.z },
            { bounds.Min.x, bounds.Max.y, bounds.Max.z }
        };

        // 下面
        AddLine(vertices, indices, corners[0], corners[1], color);
        AddLine(vertices, indices, corners[1], corners[2], color);
        AddLine(vertices, indices, corners[2], corners[3], color);
        AddLine(vertices, indices, corners[3], corners[0], color);

        // 上面
        AddLine(vertices, indices, corners[4], corners[5], color);
        AddLine(vertices, indices, corners[5], corners[6], color);
        AddLine(vertices, indices, corners[6], corners[7], color);
        AddLine(vertices, indices, corners[7], corners[4], color);

        // 上下を結ぶ4辺
        AddLine(vertices, indices, corners[0], corners[4], color);
        AddLine(vertices, indices, corners[1], corners[5], color);
        AddLine(vertices, indices, corners[2], corners[6], color);
        AddLine(vertices, indices, corners[3], corners[7], color);
    }

private:
    BroadPhase m_BroadPhase;
    Ref<Material> m_Material;
};

} // namespace Raven::ph
