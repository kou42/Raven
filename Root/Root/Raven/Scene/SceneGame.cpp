#include "SceneGame.h"

#include "Raven/Core/Input.h"
#include "Raven/Core/KeyCodes.h"
#include "Raven/Core/MouseCodes.h"
#include "Raven/Math/MathMatrix.h"
#include "Raven/Renderer/Mesh/Deformation/MeshDeformationInstance.h"
#include "Raven/Renderer/Mesh/Deformation/MeshDeformationSystem.h"
#include "Raven/Renderer/Mesh/Deformation/WaveMeshDeformer.h"
#include "Raven/Renderer/Mesh/PrimitiveMeshFactory.h"
#include "Raven/Renderer/Pipeline/Pipeline.h"
#include "Raven/Renderer/RenderCommand.h"
#include "Raven/Renderer/Renderer.h"

#include <algorithm>
#include <cmath>
#include <random>

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
} // namespace

int SceneGame::ComputeOptimizedSpawnCount() const
{
    const float side = 2.0f * m_SpawnRangeXZ;
    const float spawnArea = side * side;
    int count = static_cast<int>(spawnArea * m_TargetSphereDensity);
    count = std::max(count, m_MinSphereCount);
    count = std::min(count, m_MaxSphereCount);
    return count;
}

void SceneGame::SpawnSphereBatch(int count)
{
    if (!m_SphereMesh || !m_Material || count <= 0)
    {
        return;
    }

    m_SphereBodies.reserve(m_SphereBodies.size() + static_cast<size_t>(count));
    m_SpawnedEntities.reserve(m_SpawnedEntities.size() + static_cast<size_t>(count));
    m_SphereBodyIndexByEntity.reserve(m_SphereBodyIndexByEntity.size() + static_cast<size_t>(count));

    for (int i = 0; i < count; ++i)
    {
        Entity sphere = CreateEntity("Sphere");

        const float scale = RandomRange(m_SphereScaleMin, m_SphereScaleMax);
        const math::Vec3 tint{
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

        ColliderComponent collider{};
        collider.Type = ColliderType::Sphere;
        collider.Radius = m_SphereRadius * scale;
        collider.Restitution = 0.8f;
        collider.StaticFriction = 0.1f;
        collider.DynamicFriction = 0.1f;
        sphere.AddComponent<ColliderComponent>(collider);

        SphereBody body{};
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
    // ドラッグ中のEntityがこのBatchに含まれている可能性があるため、
    // Entity破棄より先に選択状態を解除して古いHandle/作用点を保持しないようにします。
    m_DraggedEntity = {};
    m_DragHitPoint = {};
    m_WasLeftMousePressed = Input::IsMouseButtonPressed(Mouse::Left);

    for (const SphereBody& body : m_SphereBodies)
    {
        if (body.EntityHandle)
        {
            DestroyEntity(body.EntityHandle);
        }
    }

    m_SpawnedEntities.erase(
        std::remove_if(
            m_SpawnedEntities.begin(),
            m_SpawnedEntities.end(),
            [this](const Entity& entity)
            {
                return std::any_of(
                    m_SphereBodies.begin(),
                    m_SphereBodies.end(),
                    [&entity](const SphereBody& body)
                    {
                        return body.EntityHandle == entity;
                    });
            }),
        m_SpawnedEntities.end());

    m_SphereBodies.clear();
    m_SphereBodyIndexByEntity.clear();
}

void SceneGame::SpawnBoxTestBody()
{
    if (!m_BoxMesh || !m_Material)
    {
        return;
    }

    // PrimitiveMeshFactory::CreateCube() は各軸[-0.5,+0.5]のUnit Cubeです。
    // ColliderComponent::HalfExtentsはTransform.Scaleと自動連動しないため、
    // ここで0.5 * Scaleを明示して見た目とColliderを一致させます。
    const math::Vec3 boxScale{ 10.0f, 10.0f, 10.0f };

    Entity box = CreateEntity("PhysicsTestBox");
    auto& transform = box.GetComponent<TransformComponent>();
    transform.Position = { 0.0f, 18.0f, 0.0f };
    transform.Rotation = { 0.25f, 0.35f, 0.10f };
    transform.Scale = boxScale;

    box.AddComponent<MeshRendererComponent>(MeshRendererComponent{ m_BoxMesh, m_Material });

    RigidBodyComponent rigidBody{};
    rigidBody.SetBodyType(BodyType::Dynamic);
    rigidBody.SetMass(2.0f);
    rigidBody.LinearDamping = 0.02f;
    rigidBody.AngularDamping = 0.05f;
    rigidBody.AngularVelocity = { 0.35f, 0.55f, 0.20f };
    rigidBody.UseGravity = true;
    rigidBody.AllowSleep = true;
    box.AddComponent<RigidBodyComponent>(rigidBody);

    ColliderComponent collider{};
    collider.Type = ColliderType::Box;
    collider.HalfExtents = boxScale * 0.5f;
    collider.Restitution = 0.25f;
    collider.StaticFriction = 0.6f;
    collider.DynamicFriction = 0.4f;
    box.AddComponent<ColliderComponent>(collider);

    m_BoxEntity = box;
    m_SpawnedEntities.push_back(box);
}

bool SceneGame::BuildMouseRay(
    const math::Vec2& screenPoint,
    math::Vec3& outOrigin,
    math::Vec3& outDirection) const
{
    if (m_ViewportWidth <= 0.0f || m_ViewportHeight <= 0.0f)
    {
        return false;
    }

    // ========================================================================
    // Screen pixel -> Normalized Device Coordinates
    // ========================================================================
    // Inputのマウス座標は左上原点でYが下向き正です。
    // OpenGLのNDCは画面中央原点でYが上向き正なので、Yだけ符号を反転します。
    const float ndcX = (2.0f * screenPoint.x / m_ViewportWidth) - 1.0f;
    const float ndcY = 1.0f - (2.0f * screenPoint.y / m_ViewportHeight);

    const float aspect = m_ViewportWidth / m_ViewportHeight;
    const float halfTanFovY = std::tan(m_CameraFovY * 0.5f);

    // ========================================================================
    // View matrix -> Camera world basis
    // ========================================================================
    // Raven::Mat4::LookAt()は各行に Right / Up / -Forward を格納します。
    // したがって逆行列を毎フレーム作らなくても、行列から直接Camera基底を復元できます。
    const math::Vec3 cameraRight{ m_View[0][0], m_View[0][1], m_View[0][2] };
    const math::Vec3 cameraUp{ m_View[1][0], m_View[1][1], m_View[1][2] };
    const math::Vec3 cameraForward{ -m_View[2][0], -m_View[2][1], -m_View[2][2] };

    // LookAtの平行移動成分は
    //   row0.w = -dot(Right, Eye)
    //   row1.w = -dot(Up, Eye)
    //   row2.w =  dot(Forward, Eye)
    // なので、直交基底の線形結合からEyeを復元できます。
    outOrigin =
        cameraRight * (-m_View[0][3])
        + cameraUp * (-m_View[1][3])
        + cameraForward * m_View[2][3];

    // Perspective投影面上の点をCamera基底でWorld方向へ戻します。
    // center(ndc=0,0)ではそのままcameraForwardになります。
    outDirection =
        cameraRight * (ndcX * aspect * halfTanFovY)
        + cameraUp * (ndcY * halfTanFovY)
        + cameraForward;

    if (outDirection.LengthSq() <= math::Epsilon * math::Epsilon)
    {
        return false;
    }

    outDirection.Normalize();
    return true;
}

void SceneGame::UpdateMouseDragImpulse()
{
    const bool leftPressed = Input::IsMouseButtonPressed(Mouse::Left);
    const auto [mouseX, mouseY] = Input::GetMousePosition();
    const math::Vec2 currentMouse{ mouseX, mouseY };

    const bool pressedThisFrame = leftPressed && !m_WasLeftMousePressed;
    const bool releasedThisFrame = !leftPressed && m_WasLeftMousePressed;

    ph::PhysicsWorld* physicsWorld = &GetPhysicsWorld();

    if (pressedThisFrame)
    {
        m_DragStartScreen = currentMouse;
        m_DraggedEntity = {};
        m_DragHitPoint = {};

        math::Vec3 rayOrigin{};
        math::Vec3 rayDirection{};

        if (physicsWorld != nullptr
            && BuildMouseRay(currentMouse, rayOrigin, rayDirection))
        {
            ph::PhysicsRayCastHit hit{};

            // ====================================================================
            // Physics Ray Picking
            // ====================================================================
            // 旧実装の「画面上の中心距離」では、手前/奥に重なったColliderや回転Boxを
            // 正確に選べませんでした。ここではBroad Phase + 実Collider判定を通る
            // PhysicsWorld::RayCast()を使い、本当にマウスRayが当たった最短Colliderを選びます。
            if (physicsWorld->RayCast(
                    *this,
                    rayOrigin,
                    rayDirection,
                    m_MouseRayMaxDistance,
                    hit))
            {
                if (hit.HitEntity
                    && hit.HitEntity.HasComponent<RigidBodyComponent>()
                    && hit.HitEntity.HasComponent<ColliderComponent>())
                {
                    const auto& rigidBody = hit.HitEntity.GetComponent<RigidBodyComponent>();
                    const auto& collider = hit.HitEntity.GetComponent<ColliderComponent>();

                    // 床などStatic/Kinematic BodyはRayを遮ることはありますが、
                    // マウスで投げる対象としては選択しません。
                    if (rigidBody.Type == BodyType::Dynamic
                        && rigidBody.InverseMass > 0.0f
                        && collider.Type != ColliderType::Plane)
                    {
                        m_DraggedEntity = hit.HitEntity;

                        // このPointが非常に重要です。
                        // Entity中心ではなく「実際にクリックしたCollider表面位置」を保持することで、
                        // release時にAddImpulseAtPoint()が r x J を計算し、自然な回転を発生させます。
                        m_DragHitPoint = hit.Point;
                    }
                }
            }
        }
    }

    if (releasedThisFrame && m_DraggedEntity)
    {
        const math::Vec2 drag = currentMouse - m_DragStartScreen;
        const float dragLength = drag.Length();

        if (physicsWorld != nullptr && dragLength >= m_MinDragPixels)
        {
            // ====================================================================
            // Screen-space drag -> World-space impulse
            // ====================================================================
            // X方向をCamera Right、画面の下向きYを-Camera Upへ対応させます。
            // これによりCameraが斜めでも「見た目のドラッグ方向」に物体が飛びます。
            const math::Vec3 cameraRight{ m_View[0][0], m_View[0][1], m_View[0][2] };
            const math::Vec3 cameraUp{ m_View[1][0], m_View[1][1], m_View[1][2] };

            math::Vec3 worldDirection = cameraRight * drag.x - cameraUp * drag.y;
            if (worldDirection.LengthSq() > math::Epsilon * math::Epsilon)
            {
                worldDirection.Normalize();

                const float clampedPixels = std::min(dragLength, m_MaxDragPixels);
                const float impulseMagnitude = clampedPixels * m_DragImpulsePerPixel;
                const math::Vec3 impulse = worldDirection * impulseMagnitude;

                // AddImpulseAtPoint()内部では
                //   Linear:  DeltaV     = J * inverseMass
                //   Angular: DeltaOmega = I^-1 * (r x J)
                // が適用されます。
                // したがってSphereの端やBoxの角を掴んで投げると、作用点に応じて回転します。
                physicsWorld->AddImpulseAtPoint(
                    *this,
                    m_DraggedEntity,
                    impulse,
                    m_DragHitPoint);
            }
        }

        m_DraggedEntity = {};
        m_DragHitPoint = {};
    }

    // Entity破棄後に古いGenerationのHandleを保持し続けないようにします。
    if (m_DraggedEntity && !IsEntityAlive(m_DraggedEntity))
    {
        m_DraggedEntity = {};
        m_DragHitPoint = {};
    }

    m_WasLeftMousePressed = leftPressed;
}

void SceneGame::OnCreate()
{
    m_Shader = m_ShaderLibrary.Load(
        "Test",
        "Raven/Assets/Shaders/Vertex/test.vert",
        "Raven/Assets/Shaders/Fragment/test.frag");

    m_Texture = m_TextureLibrary.Load(
        "Mountain",
        "Raven/Assets/Images/test/mountain1.png");

    PipelineSpecification pipelineSpecification{};
    pipelineSpecification.DebugName = "SceneGame Geometry Pipeline";
    pipelineSpecification.Shader = m_Shader;
    pipelineSpecification.Topology = PrimitiveTopology::Triangles;
    pipelineSpecification.Cull = CullMode::None;
    pipelineSpecification.FrontFaceMode = FrontFace::CounterClockwise;
    pipelineSpecification.DepthTest = true;
    pipelineSpecification.DepthWrite = true;
    pipelineSpecification.DepthCompare = DepthCompareOperator::Less;
    pipelineSpecification.Blend = true;

    m_Material = CreateRef<Material>(Pipeline::Create(pipelineSpecification));
    m_Material->SetUniform("u_Alpha", 1.0f);

    const math::Vec3 eye{ 0.0f, 40.0f, 80.0f };
    const math::Vec3 target{ 0.0f, 0.0f, 0.0f };
    const math::Vec3 up{ 0.0f, 1.0f, 0.0f };
    m_View = math::Mat4::LookAt(eye, target, up);
    m_Projection = math::Mat4::Perspective(
        m_CameraFovY,
        m_ViewportWidth / m_ViewportHeight,
        0.1f,
        1000.0f);
    m_Material->SetUniform("u_View", m_View);
    m_Material->SetUniform("u_Projection", m_Projection);

    const float floorVertices[] = {
        -0.5f,0.0f,-0.5f,  0.4f,0.7f,0.4f,  0.0f,0.0f,
         0.5f,0.0f,-0.5f,  0.3f,0.6f,0.3f,  1.0f,0.0f,
         0.5f,0.0f, 0.5f,  0.4f,0.7f,0.4f,  1.0f,1.0f,
        -0.5f,0.0f, 0.5f,  0.3f,0.6f,0.3f,  0.0f,1.0f
    };
    const uint32_t floorIndices[] = { 0,1,2, 2,3,0 };

    m_VertexArray = VertexArray::Create();
    auto floorVB = VertexBuffer::Create(floorVertices, sizeof(floorVertices));
    floorVB->SetLayout({
        { ShaderDataType::Float3, "a_Position" },
        { ShaderDataType::Float3, "a_Color" },
        { ShaderDataType::Float2, "a_Texcord" }
    });
    auto floorIB = IndexBuffer::Create(floorIndices, 6);
    m_VertexArray->AddVertexBuffer(floorVB);
    m_VertexArray->SetIndexBuffer(floorIB);
    m_Mesh = CreateRef<Mesh>(m_VertexArray, 6);

    m_ShadowVertexArray = VertexArray::Create();
    auto shadowVB = VertexBuffer::Create(floorVertices, sizeof(floorVertices));
    shadowVB->SetLayout({
        { ShaderDataType::Float3, "a_Position" },
        { ShaderDataType::Float3, "a_Color" },
        { ShaderDataType::Float2, "a_Texcord" }
    });
    auto shadowIB = IndexBuffer::Create(floorIndices, 6);
    m_ShadowVertexArray->AddVertexBuffer(shadowVB);
    m_ShadowVertexArray->SetIndexBuffer(shadowIB);
    m_ShadowMesh = CreateRef<Mesh>(m_ShadowVertexArray, 6);

    PipelineSpecification shadowPipelineSpecification = pipelineSpecification;
    shadowPipelineSpecification.DebugName = "SceneGame Shadow Pipeline";
    shadowPipelineSpecification.DepthWrite = false;
    shadowPipelineSpecification.DepthCompare = DepthCompareOperator::LessEqual;
    m_ShadowMaterial = CreateRef<Material>(Pipeline::Create(shadowPipelineSpecification));
    m_ShadowMaterial->SetUniform("u_Tint", math::Vec3{ 0.0f, 0.0f, 0.0f });
    m_ShadowMaterial->SetUniform("u_Alpha", 0.35f);

    m_SphereMesh = PrimitiveMeshFactory::CreateSphere();
    m_BoxMesh = PrimitiveMeshFactory::CreateCube();

    m_SpawnedEntities.clear();
    m_SphereBodies.clear();
    m_SphereBodyIndexByEntity.clear();
    m_WasSpacePressed = false;
    m_WasLeftMousePressed = false;
    m_DraggedEntity = {};
    m_DragHitPoint = {};

    Entity floor = CreateEntity("Floor");
    auto& floorTransform = floor.GetComponent<TransformComponent>();
    floorTransform.Position = { 0.0f, m_FloorY, 0.0f };
    floorTransform.Scale = { 100.0f, 1.0f, 100.0f };
    floor.AddComponent<MeshRendererComponent>(MeshRendererComponent{ m_Mesh, m_Material });

    ColliderComponent floorCollider{};
    floorCollider.Type = ColliderType::Plane;
    floorCollider.PlaneNormal = { 0.0f, 1.0f, 0.0f };
    floorCollider.PlaneOffset = 0.0f;
    floorCollider.Restitution = 0.25f;
    floorCollider.StaticFriction = 0.6f;
    floorCollider.DynamicFriction = 0.4f;
    floor.AddComponent<ColliderComponent>(floorCollider);

    m_FloorEntity = floor;
    m_SpawnedEntities.push_back(floor);

    // ========================================================================
    // Deformation validation entity
    // ========================================================================
    // ここでは「Sceneが具体的な頂点更新を直接行わない」ことを確認するため、
    // Dynamic Grid + WaveMeshDeformerをECS Component経由で接続します。
    //
    // 実際の毎フレーム更新はMeshDeformationSystemが担当し、SceneGameは
    // どのMeshとDeformerを組み合わせるかという初期構成だけを担当します。
    Ref<Mesh> waveMesh = PrimitiveMeshFactory::CreateDynamicGrid(32, 32);
    Entity waveEntity = CreateEntity("WaveDeformationGrid");
    auto& waveTransform = waveEntity.GetComponent<TransformComponent>();
    waveTransform.Position = { 0.0f, 3.0f, -22.0f };
    waveTransform.Scale = { 28.0f, 8.0f, 28.0f };

    waveEntity.AddComponent<MeshRendererComponent>(MeshRendererComponent{ waveMesh, m_Material });

    auto waveInstance = std::make_shared<MeshDeformationInstance>(
        waveMesh,
        CreateScope<WaveMeshDeformer>(0.18f, 10.0f, 2.4f));

    waveEntity.AddComponent<MeshDeformationComponent>(
        MeshDeformationComponent{ std::move(waveInstance), true });

    m_SpawnedEntities.push_back(waveEntity);

    SpawnSphereBatch(ComputeOptimizedSpawnCount());
    SpawnBoxTestBody();
    SpawnAnimationTestCube();
}

void SceneGame::OnDestroy()
{
    m_DraggedEntity = {};
    m_DragHitPoint = {};
    m_layers.clear();

    for (Entity entity : m_SpawnedEntities)
    {
        if (entity)
        {
            DestroyEntity(entity);
        }
    }
    m_SpawnedEntities.clear();

    m_AnimationTestEntity = {};
    m_BoxEntity = {};
    m_FloorEntity = {};
    m_Mesh.reset();
    m_Material.reset();
    m_ShadowMesh.reset();
    m_ShadowMaterial.reset();
    m_VertexArray.reset();
    m_ShadowVertexArray.reset();
    m_Texture.reset();
    m_Shader.reset();
    m_SphereMesh.reset();
    m_BoxMesh.reset();
    m_SphereBodies.clear();
    m_SphereBodyIndexByEntity.clear();
}

void SceneGame::OnUpdateGame(float dt)
{
    const float safeDt = std::clamp(dt, 0.0f, 0.05f);

    const bool spacePressed = Input::IsKeyPressed(Key::Space);
    if (spacePressed && !m_WasSpacePressed)
    {
        ClearSphereBatch();
        SpawnSphereBatch(ComputeOptimizedSpawnCount());
    }
    m_WasSpacePressed = spacePressed;

    // Scene::OnUpdate()は OnUpdateGame -> OnUpdatePhysics の順です。
    // AddImpulseAtPoint()は速度へ即時反映するため、releaseした同じフレームの
    // PhysicsWorld::Step()から衝突・重力・回転計算へ参加します。
    UpdateMouseDragImpulse();

    // ========================================================================
    // ECS Deformation Update
    // ========================================================================
    // SceneGameはWaveMeshDeformerをここで直接呼びません。
    // MeshDeformationComponentを持つ全EntityをSystemが走査するため、将来Skeletal/Morphを
    // 追加しても、この更新箇所は変更せず同じ入口を共有できます。
    MeshDeformationSystem::Update(*this, safeDt);

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

    for (const Entity& entity : m_SpawnedEntities)
    {
        if (!entity || !entity.HasComponent<MeshRendererComponent>())
        {
            continue;
        }

        const auto& meshRenderer = entity.GetComponent<MeshRendererComponent>();
        if (!meshRenderer.IsValid())
        {
            continue;
        }

        const auto& transform = entity.GetComponent<TransformComponent>();
        meshRenderer.Material->SetUniform("u_View", m_View);
        meshRenderer.Material->SetUniform("u_Projection", m_Projection);

        math::Vec3 tint{ 1.0f, 1.0f, 1.0f };
        const auto sphereIt = m_SphereBodyIndexByEntity.find(entity.GetIndex());
        if (sphereIt != m_SphereBodyIndexByEntity.end() && sphereIt->second < m_SphereBodies.size())
        {
            tint = m_SphereBodies[sphereIt->second].Tint;
        }
        else if (entity == m_BoxEntity)
        {
            tint = { 1.0f, 0.65f, 0.30f };
        }
        meshRenderer.Material->SetUniform("u_Tint", tint);
        meshRenderer.Material->SetUniform("u_Alpha", 1.0f);

        if (entity != m_FloorEntity && m_ShadowMesh && m_ShadowMaterial)
        {
            math::Mat4 shadowTransform = math::Mat4::Identity();
            shadowTransform = math::Translate(
                shadowTransform,
                { transform.Position.x, m_FloorY + 0.001f, transform.Position.z });

            const float shadowScaleX = std::max(transform.Scale.x, 0.1f) * 1.15f;
            const float shadowScaleZ = std::max(transform.Scale.z, 0.1f) * 1.15f;
            shadowTransform = math::Scale(shadowTransform, { shadowScaleX, 1.0f, shadowScaleZ });

            m_ShadowMaterial->SetUniform("u_View", m_View);
            m_ShadowMaterial->SetUniform("u_Projection", m_Projection);
            Renderer::Draw(m_ShadowMesh, m_ShadowMaterial, shadowTransform);
        }

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
        if (e.Handled)
        {
            break;
        }
    }

    if (e.GetEventType() == EventType::WindowResize)
    {
        auto& resizeEvent = static_cast<WindowResizeEvent&>(e);
        RenderCommand::SetViewport(0, 0, resizeEvent.GetWidth(), resizeEvent.GetHeight());

        m_ViewportWidth = static_cast<float>(resizeEvent.GetWidth());
        m_ViewportHeight = static_cast<float>(resizeEvent.GetHeight());

        if (m_ViewportWidth > 0.0f && m_ViewportHeight > 0.0f)
        {
            m_Projection = math::Mat4::Perspective(
                m_CameraFovY,
                m_ViewportWidth / m_ViewportHeight,
                0.1f,
                1000.0f);
        }
    }
}

} // namespace Raven