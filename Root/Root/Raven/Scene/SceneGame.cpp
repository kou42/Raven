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
#include "Raven/Scene/SceneCameraSystem.h"

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
    if (m_SphereMesh == nullptr || m_Material == nullptr || count <= 0)
    {
        return;
    }

    // Sphere群のLifetime情報はSphereBody配列だけを正規データにします。
    // 以前はm_SpawnedEntitiesにも同じEntity Handleを複製していましたが、描画がECS Viewへ
    // 移行した現在は二重管理する理由がありません。
    m_SphereBodies.reserve(m_SphereBodies.size() + static_cast<size_t>(count));
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
    }
}

void SceneGame::ClearSphereBatch()
{
    // ドラッグ中のEntityがこのBatchに含まれている可能性があるため、
    // Entity破棄より先に選択状態を解除して古いHandle/作用点を保持しないようにします。
    m_DraggedEntity = {};
    m_DragHitPoint = {};
    m_WasLeftMousePressed = Input::IsMouseButtonPressed(Mouse::Left);

    // Sphere群の生成・破棄責務はm_SphereBodiesへ閉じています。
    // 描画対象の削除処理は不要で、DestroyEntity()によりECS Viewから自動的に外れます。
    for (const SphereBody& body : m_SphereBodies)
    {
        if (static_cast<bool>(body.EntityHandle)
            && IsEntityAlive(body.EntityHandle))
        {
            DestroyEntity(body.EntityHandle);
        }
    }

    m_SphereBodies.clear();
    m_SphereBodyIndexByEntity.clear();
}

void SceneGame::SpawnBoxTestBody()
{
    if (m_BoxMesh == nullptr || m_Material == nullptr)
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

    // 単体EntityはSceneGameが直接Handleを保持し、OnDestroy()で同じHandleを明示破棄します。
    m_BoxEntity = box;
}

SceneCamera* SceneGame::UpdateRuntimeCamera()
{
    // Primary Cameraの探索、Transform -> View変換、Viewport -> Projection更新は
    // SceneCameraSystemへ集約します。SceneGameは行列をコピーせずCameraそのものを利用します。
    SceneCamera* runtimeCamera = SceneCameraSystem::UpdatePrimaryCamera(
        *this,
        m_ViewportWidth,
        m_ViewportHeight);

    if (runtimeCamera == nullptr)
    {
        return nullptr;
    }

    // Mouse RayはPerspective FOVを使って方向を構築するため、Camera設定から同期します。
    // View/Projection自体はSceneGameへ複製せず、Camera / Renderer Camera Contextを参照します。
    m_CameraFovY = runtimeCamera->GetPerspectiveVerticalFov();
    return runtimeCamera;
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

    if (static_cast<bool>(m_RuntimeCameraEntity) == false
        || IsEntityAlive(m_RuntimeCameraEntity) == false
        || m_RuntimeCameraEntity.HasComponent<CameraComponent>() == false)
    {
        return false;
    }

    // Mouse PickingはRuntime Cameraの正規データを直接参照します。
    // Renderer Camera Contextは描画中だけ有効で、入力更新はBeginScene()より前に行われるため、
    // PickingではCameraオブジェクトを参照する方がライフサイクル上も明確です。
    const Camera& camera = m_RuntimeCameraEntity.GetComponent<CameraComponent>().Camera;
    const math::Mat4& view = camera.GetViewMatrix();

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
    // Cameraから直接Viewを取得することでSceneGame側の行列ミラーを不要にします。
    const math::Vec3 cameraRight{ view[0][0], view[0][1], view[0][2] };
    const math::Vec3 cameraUp{ view[1][0], view[1][1], view[1][2] };
    const math::Vec3 cameraForward{ -view[2][0], -view[2][1], -view[2][2] };

    // LookAtの平行移動成分は
    //   row0.w = -dot(Right, Eye)
    //   row1.w = -dot(Up, Eye)
    //   row2.w =  dot(Forward, Eye)
    // なので、直交基底の線形結合からEyeを復元できます。
    outOrigin =
        cameraRight * (-view[0][3])
        + cameraUp * (-view[1][3])
        + cameraForward * view[2][3];

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

    const bool pressedThisFrame = leftPressed && m_WasLeftMousePressed == false;
    const bool releasedThisFrame = leftPressed == false && m_WasLeftMousePressed;

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
                if (static_cast<bool>(hit.HitEntity)
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

    if (releasedThisFrame && static_cast<bool>(m_DraggedEntity))
    {
        const math::Vec2 drag = currentMouse - m_DragStartScreen;
        const float dragLength = drag.Length();

        if (physicsWorld != nullptr && dragLength >= m_MinDragPixels)
        {
            if (static_cast<bool>(m_RuntimeCameraEntity)
                && IsEntityAlive(m_RuntimeCameraEntity)
                && m_RuntimeCameraEntity.HasComponent<CameraComponent>())
            {
                // ====================================================================
                // Screen-space drag -> World-space impulse
                // ====================================================================
                // Pickingと同じRuntime CameraからViewを取得し、X方向をCamera Right、
                // 画面の下向きYを-Camera Upへ対応させます。
                // これによりCameraが斜めでも「見た目のドラッグ方向」に物体が飛びます。
                const Camera& camera = m_RuntimeCameraEntity.GetComponent<CameraComponent>().Camera;
                const math::Mat4& view = camera.GetViewMatrix();
                const math::Vec3 cameraRight{ view[0][0], view[0][1], view[0][2] };
                const math::Vec3 cameraUp{ view[1][0], view[1][1], view[1][2] };

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
        }

        m_DraggedEntity = {};
        m_DragHitPoint = {};
    }

    // Entity破棄後に古いGenerationのHandleを保持し続けないようにします。
    if (static_cast<bool>(m_DraggedEntity) && IsEntityAlive(m_DraggedEntity) == false)
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

    // ========================================================================
    // Runtime Camera Entity
    // ========================================================================
    // 旧実装ではSceneGameがeye/targetからm_Viewを直接構築していました。
    // 現在はTransformComponentをCamera姿勢の正規データ、CameraComponent::Cameraを
    // Projection設定の正規データとし、SceneCameraSystemが両者を同期します。
    // View/ProjectionはSceneGameへ複製せず、描画時にRenderer Camera Contextへ渡します。
    //
    // 初期姿勢は旧Cameraと同じ (0,40,80) -> Origin を向くように設定します。
    // RavenのCamera Local Forwardは-Zで、X回転-atan(40/80)により下向きへ傾けます。
    Entity runtimeCameraEntity = CreateEntity("RuntimeCamera");
    TransformComponent& runtimeCameraTransform = runtimeCameraEntity.GetComponent<TransformComponent>();
    runtimeCameraTransform.Position = { 0.0f, 40.0f, 80.0f };
    runtimeCameraTransform.Rotation = { -0.463647609f, 0.0f, 0.0f };

    CameraComponent runtimeCameraComponent{};
    runtimeCameraComponent.Primary = true;
    runtimeCameraComponent.Camera.SetPerspective(m_CameraFovY, 0.1f, 1000.0f);
    runtimeCameraEntity.AddComponent<CameraComponent>(runtimeCameraComponent);

    m_RuntimeCameraEntity = runtimeCameraEntity;

    UpdateRuntimeCamera();

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

    // Scene再初期化時にSphere Batchと入力状態を明示的に初期化します。
    // 単体EntityのLifetimeは各Handleへ保持するため、汎用所有Listの初期化はありません。
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

    m_WaveEntity = waveEntity;

    SpawnSphereBatch(ComputeOptimizedSpawnCount());
    SpawnBoxTestBody();
    SpawnAnimationTestCube();
}

void SceneGame::OnDestroy()
{
    m_DraggedEntity = {};
    m_DragHitPoint = {};

    // ========================================================================
    // Layer-owned Entity cleanup
    // ========================================================================
    // Layerが生成したEntityはLayer自身が破棄責務を持ちます。
    // SceneGame内部Layerを先に破棄することで、HumanSkinningDebugLayer等が保持するEntityを
    // Scene本体のリソース解放より前に安全に片付けられます。
    m_layers.clear();

    // Sphere群はm_SphereBodiesだけを所有情報として使用します。
    for (const SphereBody& body : m_SphereBodies)
    {
        if (static_cast<bool>(body.EntityHandle)
            && IsEntityAlive(body.EntityHandle))
        {
            DestroyEntity(body.EntityHandle);
        }
    }
    m_SphereBodies.clear();
    m_SphereBodyIndexByEntity.clear();

    // ========================================================================
    // SceneGame-owned single Entity cleanup
    // ========================================================================
    // 単体EntityはそれぞれのHandleが所有対象を明示します。
    // 汎用「生成Entity一覧」を持たないため、何のために保持しているHandleかが型/名前から追跡できます。
    auto destroyOwnedEntity = [this](Entity& entity)
    {
        if (static_cast<bool>(entity)
            && IsEntityAlive(entity))
        {
            DestroyEntity(entity);
        }

        entity = {};
    };

    destroyOwnedEntity(m_AnimationTestEntity);
    destroyOwnedEntity(m_BoxEntity);
    destroyOwnedEntity(m_WaveEntity);
    destroyOwnedEntity(m_FloorEntity);
    destroyOwnedEntity(m_RuntimeCameraEntity);

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
}

void SceneGame::OnUpdateGame(float dt)
{
    const float safeDt = std::clamp(dt, 0.0f, 0.05f);

    // InspectorやGame LogicがCamera EntityのTransform/FOVを変更した場合、
    // Mouse Pickingより先にCamera本体へ同期し、描画と入力が同じCamera状態を見るようにします。
    UpdateRuntimeCamera();

    // StateMachine検証用ParameterをAnimationSystem実行前に更新する。
    UpdateAnimationStateMachineTest(safeDt);

    const bool spacePressed = Input::IsKeyPressed(Key::Space);
    if (spacePressed && m_WasSpacePressed == false)
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
    // Game ViewはPrimary SceneCameraを解決して、Cameraそのものを共通描画入口へ渡します。
    // View/ProjectionをSceneGameへ保存しないため、Camera状態の二重管理は発生しません。
    SceneCamera* runtimeCamera = UpdateRuntimeCamera();
    if (runtimeCamera == nullptr)
    {
        return;
    }

    RenderScene(*runtimeCamera);
}

void SceneGame::RenderScene(const Camera& camera)
{
    RenderCommand::SetClearColor(0.1f, 0.1f, 0.3f, 1.0f);
    RenderCommand::Clear();

    // ここがScene描画におけるCameraの単一入口です。
    // Renderer::Draw()とRenderer::EndScene()内のDebug Passは、このContextを共通利用します。
    Renderer::BeginScene(camera);

    // ========================================================================
    // ECS Mesh Rendering
    // ========================================================================
    // 描画対象はEntity所有情報とは完全に独立し、
    // TransformComponent + MeshRendererComponentを持つ生存Entityを直接走査します。
    //
    // これによりSceneGame自身が生成したSphere/Box/Waveだけでなく、Application Layerから生成された
    // Cloth / Jelly / Soft-Rigid連成Sphereも、MeshRendererComponentを追加するだけでGame Viewと
    // Scene Viewの両方へ自動的に参加します。描画登録用の別リストとの同期は不要です。
    for (auto [entity, transform, meshRenderer] : View<TransformComponent, MeshRendererComponent>())
    {
        if (meshRenderer.IsValid() == false)
        {
            continue;
        }

        // SceneGame共通Materialを使う既存検証Entityだけは、従来どおりEntityごとのTintを設定します。
        // Cloth/Jellyは専用Materialを所有しているため、ここで白へ上書きせずLayer側の色設定を保持します。
        if (meshRenderer.Material == m_Material)
        {
            math::Vec3 tint{ 1.0f, 1.0f, 1.0f };
            const auto sphereIt = m_SphereBodyIndexByEntity.find(entity.GetIndex());
            if (sphereIt != m_SphereBodyIndexByEntity.end()
                && sphereIt->second < m_SphereBodies.size())
            {
                tint = m_SphereBodies[sphereIt->second].Tint;
            }
            else if (entity == m_BoxEntity)
            {
                tint = { 1.0f, 0.65f, 0.30f };
            }

            meshRenderer.Material->SetUniform("u_Tint", tint);
            meshRenderer.Material->SetUniform("u_Alpha", 1.0f);
        }

        if (entity != m_FloorEntity
            && m_ShadowMesh != nullptr
            && m_ShadowMaterial != nullptr)
        {
            math::Mat4 shadowTransform = math::Mat4::Identity();
            shadowTransform = math::Translate(
                shadowTransform,
                { transform.Position.x, m_FloorY + 0.001f, transform.Position.z });

            const float shadowScaleX = std::max(transform.Scale.x, 0.1f) * 1.15f;
            const float shadowScaleZ = std::max(transform.Scale.z, 0.1f) * 1.15f;
            shadowTransform = math::Scale(shadowTransform, { shadowScaleX, 1.0f, shadowScaleZ });

            Renderer::Draw(m_ShadowMesh, m_ShadowMaterial, shadowTransform);
        }

        // u_View/u_ProjectionはRenderer::Draw()がCamera Contextから設定します。
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

        // Aspect Ratioの再計算もSceneCameraへ委譲します。
        // Projection式をSceneGame側へ重複実装せず、CameraのProjection設定を正規データとして扱います。
        if (m_ViewportWidth > 0.0f && m_ViewportHeight > 0.0f)
        {
            UpdateRuntimeCamera();
        }
    }
}

} // namespace Raven
