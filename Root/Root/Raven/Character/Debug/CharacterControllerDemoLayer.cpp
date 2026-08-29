// Raven/Character/Debug/CharacterControllerDemoLayer.cpp
#include "Raven/Character/Debug/CharacterControllerDemoLayer.h"

#include <algorithm>
#include <iostream>
#include <string>

#include "Raven/Core/Application.h"
#include "Raven/Renderer/Material/Material.h"
#include "Raven/Renderer/Mesh/Mesh.h"
#include "Raven/Renderer/Mesh/PrimitiveMeshFactory.h"
#include "Raven/Renderer/Pipeline/Pipeline.h"
#include "Raven/Renderer/Shader/Shader.h"
#include "Raven/Scene/Components.h"
#include "Raven/Scene/Scene.h"

namespace Raven
{

void CharacterControllerDemoLayer::OnAttach()
{
    Scene* scene = m_Application.GetScene();
    if (scene == nullptr)
    {
        return;
    }

    // ========================================================================
    // Character visual resource
    // ========================================================================
    // CharacterControllerの衝突形状は内部Capsule Castであり、表示Meshとは独立しています。
    // 現段階ではPrimitiveMeshFactoryにCapsule Meshが無いため、Capsuleの外接寸法に合わせた
    // Cubeを表示し、「足元位置・向き・移動」を分かりやすく確認できるようにします。
    m_CharacterMesh = PrimitiveMeshFactory::CreateCube();
    if (m_CharacterMesh == nullptr)
    {
        return;
    }

    ShaderLibrary shaderLibrary{};
    Ref<Shader> shader = shaderLibrary.Load(
        "CharacterControllerDemo",
        "Raven/Assets/Shaders/Vertex/test.vert",
        "Raven/Assets/Shaders/Fragment/test.frag");
    if (shader == nullptr)
    {
        m_CharacterMesh.reset();
        return;
    }

    PipelineSpecification pipelineSpecification{};
    pipelineSpecification.DebugName = "Character Controller Demo Pipeline";
    pipelineSpecification.Shader = shader;
    pipelineSpecification.Topology = PrimitiveTopology::Triangles;
    pipelineSpecification.Cull = CullMode::None;
    pipelineSpecification.FrontFaceMode = FrontFace::CounterClockwise;
    pipelineSpecification.DepthTest = true;
    pipelineSpecification.DepthWrite = true;
    pipelineSpecification.DepthCompare = DepthCompareOperator::Less;
    pipelineSpecification.Blend = true;

    m_CharacterMaterial = CreateRef<Material>(Pipeline::Create(pipelineSpecification));
    m_CharacterMaterial->SetUniform("u_Tint", math::Vec3{ 0.20f, 0.65f, 1.00f });
    m_CharacterMaterial->SetUniform("u_Alpha", 1.0f);

    // ========================================================================
    // Character Controller root
    // ========================================================================
    // CharacterControllerではTransform::PositionをCapsuleの中心ではなく「足元Root」として扱います。
    // SceneGameの床はY=0なので、初期RootもY=0へ置けば最初のGround Queryで安定して接地できます。
    // 他の検証Entityと重なりにくいよう、初期位置だけ+Zへ離しています。
    m_CharacterRootTransform = TransformComponent{};
    m_CharacterRootTransform.Position = { 0.0f, 0.0f, 18.0f };
    m_CharacterRootTransform.Rotation = { 0.0f, 0.0f, 0.0f };
    m_CharacterRootTransform.Scale = { 1.0f, 1.0f, 1.0f };

    m_CharacterController = CharacterController{};

    // ========================================================================
    // Visual Entity
    // ========================================================================
    // ColliderComponent / RigidBodyComponentは意図的に追加しません。
    // CharacterのCollisionはCharacterController::UpdateWithMovingPlatforms()内のCapsule Castが担当し、
    // 表示EntityまでPhysicsWorldへ登録すると同じCharacterが二重の衝突形状を持ってしまうためです。
    m_CharacterEntity = scene->CreateEntity("Gamepad Character Controller Demo");
    m_CharacterEntity.AddComponent<MeshRendererComponent>(
        MeshRendererComponent{ m_CharacterMesh, m_CharacterMaterial });

    SyncVisualTransform();

    std::cout
        << "[CharacterController] Gamepad Controls: Left Stick Move, A Jump, RT Run\n";
}

void CharacterControllerDemoLayer::OnDetach()
{
    Scene* scene = m_Application.GetScene();
    if (scene != nullptr
        && static_cast<bool>(m_CharacterEntity)
        && scene->IsEntityAlive(m_CharacterEntity))
    {
        scene->DestroyEntity(m_CharacterEntity);
    }

    m_CharacterEntity = {};
    m_CharacterMesh.reset();
    m_CharacterMaterial.reset();
    m_CharacterController = CharacterController{};
    m_CharacterRootTransform = TransformComponent{};
}

void CharacterControllerDemoLayer::OnUpdate(float deltaTime)
{
    Scene* scene = m_Application.GetScene();
    if (scene == nullptr
        || static_cast<bool>(m_CharacterEntity) == false
        || scene->IsEntityAlive(m_CharacterEntity) == false)
    {
        return;
    }

    // Scene側と同じく極端に大きなFrame DeltaをClampします。
    // Debugger停止後などの単発巨大dtでCharacterが長距離Capsule Castすることを防ぎます。
    const float safeDeltaTime = std::clamp(deltaTime, 0.0f, 0.05f);

    // ========================================================================
    // Gamepad -> Device-independent Character input
    // ========================================================================
    // Input/WindowsInput層がGLFW固有のGamepad状態をGamepadStateへ変換し、
    // ReadDefaultGamepadInput()がDead Zone処理とGameplay Button Mappingを担当します。
    // CharacterController本体はGamepad APIを一切知らず、CharacterControllerInputだけを受け取ります。
    const CharacterControllerInput input = CharacterController::ReadDefaultGamepadInput();

    std::string errorMessage;
    if (m_CharacterController.UpdateWithMovingPlatforms(
            input,
            safeDeltaTime,
            *scene,
            m_CharacterRootTransform,
            1.0f,
            &errorMessage) == false)
    {
        std::cerr
            << "[CharacterController] Gamepad Character更新に失敗しました: "
            << errorMessage << '\n';
        return;
    }

    SyncVisualTransform();
}

void CharacterControllerDemoLayer::SyncVisualTransform()
{
    if (static_cast<bool>(m_CharacterEntity) == false
        || m_CharacterEntity.HasComponent<TransformComponent>() == false)
    {
        return;
    }

    const CharacterControllerConfig& config = m_CharacterController.GetConfig();

    // Capsule全高 = Cylinder部分(2 * HalfLength) + 上下Hemisphere(2 * Radius)。
    // Primitive Cubeは原点中心なので、その半分だけ足元Rootから上へ配置します。
    const float capsuleHeight = 2.0f * (config.CapsuleHalfLength + config.CapsuleRadius);
    const float capsuleDiameter = 2.0f * config.CapsuleRadius;

    TransformComponent& visualTransform = m_CharacterEntity.GetComponent<TransformComponent>();
    visualTransform.Position = m_CharacterRootTransform.Position
        + math::Vec3{ 0.0f, capsuleHeight * 0.5f, 0.0f };
    visualTransform.Rotation = m_CharacterRootTransform.Rotation;
    visualTransform.Scale = math::Vec3{ capsuleDiameter, capsuleHeight, capsuleDiameter };
}

} // namespace Raven
