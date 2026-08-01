#include "SceneGame.h"
#include "Raven/Renderer/Renderer.h"
#include "Raven/Renderer/RenderCommand.h"
#include "Raven/Renderer/Pipeline/Pipeline.h"
#include "Raven/Core/Input.h"
#include "Raven/Core/KeyCodes.h"
#include "Raven/Math/MathMatrix.h"
#include "Raven/Math/MathUtility.h"

#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

namespace Raven
{

namespace
{
float RandomRange(float minValue, float maxValue)
{
    static std::mt19937 generator(std::random_device{}());
    std::uniform_real_distribution<float> distribution(minValue, maxValue);
    return distribution(generator);
}
}

int SceneGame::ComputeOptimizedSpawnCount() const
{
    const float side = 2.0f * m_SpawnRangeXZ;
    const float spawnArea = side * side;
    int count = static_cast<int>(spawnArea * m_TargetSphereDensity);

    if (count < m_MinSphereCount) {
        count = m_MinSphereCount;
    }
    if (count > m_MaxSphereCount) {
        count = m_MaxSphereCount;
    }
    return count;
}

void SceneGame::SpawnSphereBatch(int count)
{
    if (!m_SphereMesh || !m_Material || count <= 0) {
        return;
    }

    m_SphereBodies.reserve(m_SphereBodies.size() + static_cast<size_t>(count));
    m_SpawnedEntities.reserve(m_SpawnedEntities.size() + static_cast<size_t>(count));
    m_SphereBodyIndexByEntity.reserve(m_SphereBodyIndexByEntity.size() + static_cast<size_t>(count));

    for (int i = 0; i < count; ++i)
    {
        Entity sphere = CreateEntity("Sphere");

        const float scale = RandomRange(m_SphereScaleMin, m_SphereScaleMax);
        const math::Vec3 tint = {
            RandomRange(0.45f, 1.0f),
            RandomRange(0.45f, 1.0f),
            RandomRange(0.45f, 1.0f)
        };

        auto& transform = sphere.GetComponent<TransformComponent>();
        transform.Position = {
            RandomRange(-m_SpawnRangeXZ, m_SpawnRangeXZ),
            RandomRange(m_SpawnHeightMin, m_SpawnHeightMax),
            RandomRange(-m_SpawnRangeXZ, m_SpawnRangeXZ)
        };
        transform.Scale = { scale, scale, scale };

        sphere.AddComponent<MeshRendererComponent>(MeshRendererComponent{ m_SphereMesh, m_Material });

        SphereBody body;
        body.EntityHandle = sphere;
        body.Velocity = {
            RandomRange(m_InitialVelocityXMin, m_InitialVelocityXMax),
            0.0f,
            RandomRange(m_InitialVelocityZMin, m_InitialVelocityZMax)
        };
        body.Tint = tint;
        body.Radius = m_SphereRadius * scale;

        const size_t bodyIndex = m_SphereBodies.size();
        m_SphereBodyIndexByEntity[sphere.GetID()] = bodyIndex;

        m_SphereBodies.push_back(body);
        m_SpawnedEntities.push_back(sphere);
    }
}

void SceneGame::ClearSphereBatch()
{
    for (const SphereBody& body : m_SphereBodies)
    {
        if (body.EntityHandle) {
            DestroyEntity(body.EntityHandle);
        }
    }

    m_SpawnedEntities.erase(
        std::remove_if(m_SpawnedEntities.begin(), m_SpawnedEntities.end(),
            [this](const Entity& entity)
            {
                return std::any_of(m_SphereBodies.begin(), m_SphereBodies.end(),
                    [&entity](const SphereBody& body)
                    {
                        return body.EntityHandle == entity;
                    });
            }),
        m_SpawnedEntities.end());

    m_SphereBodies.clear();
    m_SphereBodyIndexByEntity.clear();
}

void SceneGame::OnCreate()
{
    // 広大な床パネル（XZ平面、Y=0）
    float vertices[] =
    {
        // position              // color              // uv
        -0.5f,  0.0f, -0.5f,    0.4f, 0.7f, 0.4f,    0.0f, 0.0f,
         0.5f,  0.0f, -0.5f,    0.3f, 0.6f, 0.3f,    1.0f, 0.0f,
         0.5f,  0.0f,  0.5f,    0.4f, 0.7f, 0.4f,    1.0f, 1.0f,
        -0.5f,  0.0f,  0.5f,    0.3f, 0.6f, 0.3f,    0.0f, 1.0f,
    };

    uint32_t indices[] =
    {
        0, 1, 2,
        2, 3, 0
    };

    m_VertexArray = VertexArray::Create();

    uint32_t indexCount = sizeof(indices) / sizeof(uint32_t);
    auto vertexBuffer = VertexBuffer::Create(vertices, sizeof(vertices));
    vertexBuffer->SetLayout({
        { ShaderDataType::Float3, "a_Position" },
        { ShaderDataType::Float3, "a_Color" },
        { ShaderDataType::Float2, "a_Texcord" }
    });

    auto indexBuffer = IndexBuffer::Create(indices, indexCount);
    m_VertexArray->AddVertexBuffer(vertexBuffer);
    m_VertexArray->SetIndexBuffer(indexBuffer);

    m_Shader = m_ShaderLibrary.Load(
        "Test",
        "Raven/Assets/Shaders/Vertex/test.vert",
        "Raven/Assets/Shaders/Fragment/test.frag"
    );

    m_Texture = m_TextureLibrary.Load(
        "Mountain",
        "Raven/Assets/Images/test/mountain1.png"
    );

    PipelineSpecification pipelineSpecification;
    pipelineSpecification.DebugName = "SceneGame Floor Pipeline";
    pipelineSpecification.Shader = m_Shader;
    pipelineSpecification.Topology = PrimitiveTopology::Triangles;
    pipelineSpecification.Cull = CullMode::None;
    pipelineSpecification.FrontFaceMode = FrontFace::CounterClockwise;
    pipelineSpecification.DepthTest = true;
    pipelineSpecification.DepthWrite = true;
    pipelineSpecification.DepthCompare = DepthCompareOperator::Less;
    pipelineSpecification.Blend = true;

    Ref<Pipeline> pipeline = Pipeline::Create(pipelineSpecification);

    m_Material = CreateRef<Material>(pipeline);
    m_Mesh = CreateRef<Mesh>(m_VertexArray, static_cast<int32_t>(indexCount));
    m_Material->SetUniform("u_Alpha", 1.0f);

    float shadowVertices[] =
    {
        // position           // color        // uv
        -0.5f, 0.0f, -0.5f,   0.0f, 0.0f, 0.0f,   0.0f, 0.0f,
         0.5f, 0.0f, -0.5f,   0.0f, 0.0f, 0.0f,   1.0f, 0.0f,
         0.5f, 0.0f,  0.5f,   0.0f, 0.0f, 0.0f,   1.0f, 1.0f,
        -0.5f, 0.0f,  0.5f,   0.0f, 0.0f, 0.0f,   0.0f, 1.0f,
    };

    uint32_t shadowIndices[] =
    {
        0, 1, 2,
        2, 3, 0
    };

    m_ShadowVertexArray = VertexArray::Create();
    auto shadowVertexBuffer = VertexBuffer::Create(shadowVertices, sizeof(shadowVertices));
    shadowVertexBuffer->SetLayout({
        { ShaderDataType::Float3, "a_Position" },
        { ShaderDataType::Float3, "a_Color" },
        { ShaderDataType::Float2, "a_Texcord" }
    });
    auto shadowIndexBuffer = IndexBuffer::Create(shadowIndices, 6);
    m_ShadowVertexArray->AddVertexBuffer(shadowVertexBuffer);
    m_ShadowVertexArray->SetIndexBuffer(shadowIndexBuffer);
    m_ShadowMesh = CreateRef<Mesh>(m_ShadowVertexArray, 6);

    PipelineSpecification shadowPipelineSpecification;
    shadowPipelineSpecification.DebugName = "SceneGame Shadow Pipeline";
    shadowPipelineSpecification.Shader = m_Shader;
    shadowPipelineSpecification.Topology = PrimitiveTopology::Triangles;
    shadowPipelineSpecification.Cull = CullMode::None;
    shadowPipelineSpecification.FrontFaceMode = FrontFace::CounterClockwise;
    shadowPipelineSpecification.DepthTest = true;
    shadowPipelineSpecification.DepthWrite = false;
    shadowPipelineSpecification.DepthCompare = DepthCompareOperator::LessEqual;
    shadowPipelineSpecification.Blend = true;

    Ref<Pipeline> shadowPipeline = Pipeline::Create(shadowPipelineSpecification);
    m_ShadowMaterial = CreateRef<Material>(shadowPipeline);
    m_ShadowMaterial->SetUniform("u_View", m_View);
    m_ShadowMaterial->SetUniform("u_Projection", m_Projection);
    m_ShadowMaterial->SetUniform("u_Tint", math::Vec3{ 0.0f, 0.0f, 0.0f });
    m_ShadowMaterial->SetUniform("u_Alpha", 0.35f);

    // カメラ行列を設定
    // eye = (0, 40, 80) : カメラの位置。地面より十分上（Y = 40）かつ後方（Z = 80）に置いて、
    //                     大きな床メッシュがカメラ近傍でクリップされるのを避けます。
    // target = (0, 0, 0): カメラが見る先。原点を見下ろす構図です。
    // up = (0, 1, 0)    : カメラの「上方向」。ワールドのY軸を上として使う指定です。
    math::Vec3 eye    = { 0.0f, 40.0f, 80.0f };
    math::Vec3 target = { 0.0f,  0.0f,  0.0f };
    math::Vec3 up     = { 0.0f,  1.0f,  0.0f };

    //ワールド座標の頂点を「カメラから見た座標系」に変換する行列です。
    //要するに「世界を動かして、カメラが原点・前方固定に見える状態」にします。
    m_View       = math::Mat4::LookAt(eye, target, up);

    //fov = 0.7854 rad
    //: 視野角。約45度。値を大きくすると広角で迫力、ただし歪みが増えます。
    //aspect = 1280 / 720 ≒ 1.777...
    //: 横縦比（16 : 9）。ここが画面比とズレると、見た目が横に伸びたり縦に潰れたりします。
    //near = 0.1
    //: 手前のクリップ面。これより近いものは描画しません。
    //far = 1000.0
    //: 奥のクリップ面。これより遠いものは描画しません。
    //透視投影の本質は「遠いほど小さく見える」変換です。
	float fov = 0.7854f; // 45度 (π/4 rad)
	float aspect = 1280.0f / 720.0f; // 横縦比（16:9）
	float near = 0.1f; // 手前のクリップ面
	float far = 1000.0f; // 奥のクリップ面
    m_Projection = math::Mat4::Perspective(fov, aspect, near, far);

    // モデル座標 → ワールド座標（Model）
    // ワールド座標 → カメラ座標（View）
    // カメラ座標 → クリップ座標（Projection）
    // つまり最終的に
    //+--------------------------------------------------------------------
	// clip = Projection * View * Model * vertex_position
    //+--------------------------------------------------------------------

    m_Material->SetUniform("u_View",       m_View);
    m_Material->SetUniform("u_Projection", m_Projection);

    m_SpawnedEntities.clear();
    m_SphereBodies.clear();
    m_SphereBodyIndexByEntity.clear();
    m_WasSpacePressed = false;

    // 原点に広大なパネルを配置（100x100ユニット）
    Entity floor = CreateEntity("Floor");
    TransformComponent& transform = floor.GetComponent<TransformComponent>();
    transform.Position = { 0.0f, 0.0f, 0.0f };
    transform.Scale    = { 100.0f, 1.0f, 100.0f };
    floor.AddComponent<MeshRendererComponent>(MeshRendererComponent{ m_Mesh, m_Material });
    m_FloorEntity = floor;
    m_SpawnedEntities.push_back(floor);

    // ---- 球体メッシュの生成（UV球体, radius=0.5） ----
    {
        const int   stacks = 24;
        const int   slices = 48;
        const float radius = 0.5f;
        const float PI     = 3.14159265358979f;

        std::vector<float>    sv;
        std::vector<uint32_t> si;

        for (int i = 0; i <= stacks; ++i)
        {
            float phi = PI / 2.0f - i * PI / stacks;
            float y   = radius * sinf(phi);
            float r   = radius * cosf(phi);
            float vt  = static_cast<float>(i) / stacks;

            for (int j = 0; j <= slices; ++j)
            {
                float theta = j * 2.0f * PI / slices;
                float x     = r * cosf(theta);
                float z     = r * sinf(theta);
                float u     = static_cast<float>(j) / slices;

                // position
                sv.push_back(x);
                sv.push_back(y);
                sv.push_back(z);
                // color (青みがかった白)
                sv.push_back(0.7f + 0.3f * vt);
                sv.push_back(0.8f);
                sv.push_back(0.9f);
                // uv
                sv.push_back(u);
                sv.push_back(vt);
            }
        }

        for (int i = 0; i < stacks; ++i)
        {
            for (int j = 0; j < slices; ++j)
            {
                uint32_t a = static_cast<uint32_t>(i * (slices + 1) + j);
                uint32_t b = a + static_cast<uint32_t>(slices + 1);
                si.push_back(a);     si.push_back(b);     si.push_back(a + 1);
                si.push_back(b);     si.push_back(b + 1); si.push_back(a + 1);
            }
        }

        m_SphereVertexArray = VertexArray::Create();
        auto svb = VertexBuffer::Create(sv.data(), static_cast<uint32_t>(sv.size() * sizeof(float)));
        svb->SetLayout({
            { ShaderDataType::Float3, "a_Position" },
            { ShaderDataType::Float3, "a_Color"    },
            { ShaderDataType::Float2, "a_Texcord"  }
        });
        auto sib = IndexBuffer::Create(si.data(), static_cast<uint32_t>(si.size()));
        m_SphereVertexArray->AddVertexBuffer(svb);
        m_SphereVertexArray->SetIndexBuffer(sib);
        m_SphereMesh = CreateRef<Mesh>(m_SphereVertexArray, static_cast<int32_t>(si.size()));
    }

    SpawnSphereBatch(ComputeOptimizedSpawnCount());

}

void SceneGame::OnDestroy()
{
    // ここでは、シーンのリソースを解放する処理を行います。例えば、シェーダーやテクスチャ、頂点配列などのリソースを解放する必要があります。また、レイヤーもクリアしておくと良いでしょう。
    m_layers.clear();

    for (Entity entity : m_SpawnedEntities)
    {
        DestroyEntity(entity);
    }
    m_SpawnedEntities.clear();

    m_Mesh.reset();
    m_Material.reset();
    m_ShadowMesh.reset();
    m_ShadowMaterial.reset();
    m_VertexArray.reset();
    m_ShadowVertexArray.reset();
    m_Texture.reset();
    m_Shader.reset();
    m_SphereMesh.reset();
    m_SphereVertexArray.reset();
    m_SphereBodies.clear();
    m_SphereBodyIndexByEntity.clear();
}

void SceneGame::OnUpdate(float dt)
{
    float safeDt = dt;
    if (safeDt < 0.0f) {
        safeDt = 0.0f;
    }
    else if (safeDt > 0.05f) {
        safeDt = 0.05f;
    }

    const bool spacePressed = Input::IsKeyPressed(Key::Space);
    if (spacePressed && !m_WasSpacePressed)
    {
        ClearSphereBatch();
        SpawnSphereBatch(ComputeOptimizedSpawnCount());
    }
    m_WasSpacePressed = spacePressed;

    for (SphereBody& body : m_SphereBodies)
    {
        if (!body.EntityHandle || !body.EntityHandle.HasComponent<TransformComponent>()) {
            continue;
        }

        auto& transform = body.EntityHandle.GetComponent<TransformComponent>();

        // 半陰的オイラー積分: 先に速度を更新してから位置を更新
        body.Velocity.y += m_Gravity * safeDt;
        transform.Position += body.Velocity * safeDt;

        const float floorTop = m_FloorY + body.Radius;
        if (transform.Position.y < floorTop)
        {
            transform.Position.y = floorTop;

            if (body.Velocity.y < 0.0f)
            {
                body.Velocity.y = -body.Velocity.y * m_BounceDamping;
                body.Velocity.x *= m_BounceTangentialDamping;
                body.Velocity.z *= m_BounceTangentialDamping;

                if (std::abs(body.Velocity.y) < m_StopVelocityEpsilon)
                {
                    body.Velocity.y = 0.0f;
                }
            }

            if (body.Velocity.y == 0.0f)
            {
                const float frictionFactor = std::max(0.0f, 1.0f - m_GroundFriction * safeDt);
                body.Velocity.x *= frictionFactor;
                body.Velocity.z *= frictionFactor;

                if (std::abs(body.Velocity.x) < m_StopVelocityEpsilon) {
                    body.Velocity.x = 0.0f;
                }
                if (std::abs(body.Velocity.z) < m_StopVelocityEpsilon) {
                    body.Velocity.z = 0.0f;
                }
            }
        }
    }

    for (auto& layer : m_layers)
    {
        layer->OnUpdate(safeDt);
    }
}

void SceneGame::OnRender()
{
    RenderCommand::SetClearColor(0.1f, 0.1f, 0.3f, 1.0f);
    RenderCommand::Clear();

    Renderer::BeginScene();

    if (m_FloorEntity && m_FloorEntity.HasComponent<MeshRendererComponent>())
    {
        const auto& floorRenderer = m_FloorEntity.GetComponent<MeshRendererComponent>();
        if (floorRenderer.IsValid())
        {
            floorRenderer.Material->SetUniform("u_View", m_View);
            floorRenderer.Material->SetUniform("u_Projection", m_Projection);
            floorRenderer.Material->SetUniform("u_Tint", math::Vec3{ 1.0f, 1.0f, 1.0f });
            floorRenderer.Material->SetUniform("u_Alpha", 1.0f);

            const auto& transform = m_FloorEntity.GetComponent<TransformComponent>();
            Renderer::Draw(floorRenderer.Mesh, floorRenderer.Material, transform.GetTransform());
        }
    }

    for (const Entity& entity : m_SpawnedEntities)
    {
        if (!entity || !entity.HasComponent<MeshRendererComponent>() || entity == m_FloorEntity) {
            continue;
        }

        const auto& meshRenderer = entity.GetComponent<MeshRendererComponent>();
        if (!meshRenderer.IsValid()) {
            continue;
        }

        const auto& transform = entity.GetComponent<TransformComponent>();
        const auto& position = transform.Position;

        math::Mat4 shadowTransform = math::Mat4::Identity();
        shadowTransform = math::Translate(shadowTransform, { position.x, m_FloorY + 0.001f, position.z });

        float bodyScale = transform.Scale.x;
        if (bodyScale <= 0.0f) {
            bodyScale = 1.0f;
        }
        const float shadowScale = bodyScale * 1.15f;
        shadowTransform = math::Scale(shadowTransform, { shadowScale, 1.0f, shadowScale });

        meshRenderer.Material->SetUniform("u_View", m_View);
        meshRenderer.Material->SetUniform("u_Projection", m_Projection);

        math::Vec3 tint = { 1.0f, 1.0f, 1.0f };
        auto it = m_SphereBodyIndexByEntity.find(entity.GetID());
        if (it != m_SphereBodyIndexByEntity.end())
        {
            const size_t index = it->second;
            if (index < m_SphereBodies.size()) {
                tint = m_SphereBodies[index].Tint;
            }
        }
        meshRenderer.Material->SetUniform("u_Tint", tint);

        m_ShadowMaterial->SetUniform("u_View", m_View);
        m_ShadowMaterial->SetUniform("u_Projection", m_Projection);
        Renderer::Draw(m_ShadowMesh, m_ShadowMaterial, shadowTransform);

        Renderer::Draw(meshRenderer.Mesh, meshRenderer.Material, transform.GetTransform());
    }
    Renderer::EndScene();

    for (auto& layer : m_layers)
    {
        layer->OnRender();
    }
#if 0
    RenderCommand::SetClearColor(
        math::Vec4{ 0.1f, 0.1f, 0.1f, 1.0f }
    );

    RenderCommand::Clear();

    for (const auto& [id, meshRenderer] : scene.GetMeshRenderers())
    {
        if (meshRenderer.IsValid() == false) {
            continue;
        }

        const auto& transform =scene.GetComponent<TransformComponent>(id);

        Renderer::Draw(meshRenderer.Mesh, meshRenderer.Material, transform.GetTransform());
    }
#endif
}

void SceneGame::OnEvent(Event& e)
{
    for (auto it = m_layers.rbegin(); it != m_layers.rend(); ++it)
    {
        (*it)->OnEvent(e);

        if (e.Handled) {
            break;
        }
    }

    if (e.GetEventType() == EventType::WindowResize)
    {
        auto& resizeEvent = static_cast<WindowResizeEvent&>(e);
        RenderCommand::SetViewport(0, 0, resizeEvent.GetWidth(), resizeEvent.GetHeight());

        const float width  = static_cast<float>(resizeEvent.GetWidth());
        const float height = static_cast<float>(resizeEvent.GetHeight());
        if (height > 0.0f)
        {
            const float fov  = 0.7854f;
            const float nearPlane = 0.1f;
            const float farPlane  = 1000.0f;
            m_Projection = math::Mat4::Perspective(fov, width / height, nearPlane, farPlane);
        }
    }
}

}
