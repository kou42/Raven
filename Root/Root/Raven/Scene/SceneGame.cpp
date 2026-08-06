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

        // ====================================================================
        // Dynamic RigidBody
        // ====================================================================
        // これまでSceneGameが独自に保持していたVelocityと重力処理を廃止し、
        // PhysicsWorldがRigidBodyComponentを唯一の運動状態として更新します。
        RigidBodyComponent rigidBody{};
        rigidBody.SetBodyType(BodyType::Dynamic);
        rigidBody.SetMass(1.0f);
        rigidBody.LinearVelocity = {
            RandomRange(m_InitialVelocityXMin, m_InitialVelocityXMax),
            0.0f,
            RandomRange(m_InitialVelocityZMin, m_InitialVelocityZMax)
        };
        rigidBody.LinearDamping = 0.02f;
        rigidBody.UseGravity = true;
        rigidBody.AllowSleep = true;
        rigidBody.SleepThreshold = 0.05f;
        rigidBody.SleepTimeThreshold = 0.5f;
        sphere.AddComponent<RigidBodyComponent>(rigidBody);

        // ====================================================================
        // Sphere Collider
        // ====================================================================
        // 描画メッシュの基準半径は0.5で、Transform.Scaleにより見た目が拡大されます。
        // Collider半径も同じ倍率で拡大し、描画形状と衝突形状を一致させます。
        ColliderComponent collider{};
        collider.Type = ColliderType::Sphere;
        collider.Radius = m_SphereRadius * scale;
        collider.Restitution = 0.35f;
        collider.StaticFriction = 0.65f;
        collider.DynamicFriction = 0.45f;
        sphere.AddComponent<ColliderComponent>(collider);

        // SphereBodyは現在、描画用TintとEntity対応表だけに使用します。
        // 物理速度の正しい所有者はRigidBodyComponentです。
        SphereBody body;
        body.EntityHandle = sphere;
        body.Velocity = rigidBody.LinearVelocity;
        body.Tint = tint;
        body.Radius = collider.Radius;

        const size_t bodyIndex = m_SphereBodies.size();
        m_SphereBodyIndexByEntity[sphere.GetIndex()] = bodyIndex;

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
    math::Vec3 eye    = { 0.0f, 40.0f, 80.0f };
    math::Vec3 target = { 0.0f,  0.0f,  0.0f };
    math::Vec3 up     = { 0.0f,  1.0f,  0.0f };

    m_View = math::Mat4::LookAt(eye, target, up);

    float fov = 0.7854f;
    float aspect = 1280.0f / 720.0f;
    float near = 0.1f;
    float far = 1000.0f;
    m_Projection = math::Mat4::Perspective(fov, aspect, near, far);

    m_Material->SetUniform("u_View", m_View);
    m_Material->SetUniform("u_Projection", m_Projection);

    m_SpawnedEntities.clear();
    m_SphereBodies.clear();
    m_SphereBodyIndexByEntity.clear();
    m_WasSpacePressed = false;

    // ========================================================================
    // 無限Plane Colliderを持つ床
    // ========================================================================
    Entity floor = CreateEntity("Floor");
    TransformComponent& transform = floor.GetComponent<TransformComponent>();
    transform.Position = { 0.0f, m_FloorY, 0.0f };
    transform.Scale = { 100.0f, 1.0f, 100.0f };
    floor.AddComponent<MeshRendererComponent>(MeshRendererComponent{ m_Mesh, m_Material });

    // 床は動かないためRigidBodyComponentは不要です。
    // ContactSolverはRigidBodyを持たないColliderをInverseMass=0のStaticとして扱います。
    ColliderComponent floorCollider{};
    floorCollider.Type = ColliderType::Plane;
    floorCollider.PlaneNormal = { 0.0f, 1.0f, 0.0f };
    floorCollider.PlaneOffset = 0.0f;
    floorCollider.Restitution = 0.25f;
    floorCollider.StaticFriction = 0.8f;
    floorCollider.DynamicFriction = 0.6f;
    floor.AddComponent<ColliderComponent>(floorCollider);

    m_FloorEntity = floor;
    m_SpawnedEntities.push_back(floor);

    // ---- 球体メッシュの生成（UV球体, radius=0.5） ----
    {
        const int stacks = 24;
        const int slices = 48;
        const float radius = 0.5f;
        const float PI = 3.14159265358979f;

        std::vector<float> sv;
        std::vector<uint32_t> si;

        for (int i = 0; i <= stacks; ++i)
        {
            float phi = PI / 2.0f - i * PI / stacks;
            float y = radius * sinf(phi);
            float r = radius * cosf(phi);
            float vt = static_cast<float>(i) / stacks;

            for (int j = 0; j <= slices; ++j)
            {
                float theta = j * 2.0f * PI / slices;
                float x = r * cosf(theta);
                float z = r * sinf(theta);
                float u = static_cast<float>(j) / slices;

                sv.push_back(x);
                sv.push_back(y);
                sv.push_back(z);
                sv.push_back(0.7f + 0.3f * vt);
                sv.push_back(0.8f);
                sv.push_back(0.9f);
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
                si.push_back(a); si.push_back(b); si.push_back(a + 1);
                si.push_back(b); si.push_back(b + 1); si.push_back(a + 1);
            }
        }

        m_SphereVertexArray = VertexArray::Create();
        auto svb = VertexBuffer::Create(sv.data(), static_cast<uint32_t>(sv.size() * sizeof(float)));
        svb->SetLayout({
            { ShaderDataType::Float3, "a_Position" },
            { ShaderDataType::Float3, "a_Color" },
            { ShaderDataType::Float2, "a_Texcord" }
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

void SceneGame::OnUpdateGame(float dt)
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

    // ========================================================================
    // 物理更新はScene::OnUpdatePhysicsへ一本化
    // ========================================================================
    // Scene::OnUpdate()はこのOnUpdateGame()の後に固定タイムステップで
    // PhysicsWorld::Step()を呼びます。
    // ここで独自に重力・位置・床反射を計算すると、同じ運動が二重に適用されるため、
    // ゲーム側は入力やスポーンなど「物理への指示」だけを担当します。

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
        auto it = m_SphereBodyIndexByEntity.find(entity.GetIndex());
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

        const float width = static_cast<float>(resizeEvent.GetWidth());
        const float height = static_cast<float>(resizeEvent.GetHeight());
        if (height > 0.0f)
        {
            const float fov = 0.7854f;
            const float nearPlane = 0.1f;
            const float farPlane = 1000.0f;
            m_Projection = math::Mat4::Perspective(fov, width / height, nearPlane, farPlane);
        }
    }
}

}
