// Raven/Character/Debug/CharacterControllerDemoLayer.h
#pragma once

#include "Raven/Character/CharacterController.h"
#include "Raven/Core/Base.h"
#include "Raven/Renderer/Layer/Layer.h"
#include "Raven/Scene/Entity.h"

namespace Raven
{

class Application;
class Material;
class Mesh;

// ============================================================================
// CharacterControllerDemoLayer
// ============================================================================
// Gamepad -> CharacterControllerInput -> CharacterController -> Scene Transform
// の一連のRuntime経路を目視確認するための最小デモLayerです。
//
// CharacterController本体はKinematic Capsuleを内部のShape Castとして扱い、ECS上に
// Character用RigidBody/Colliderを生成しません。そのため表示用EntityにもColliderを付けず、
// Character自身のCapsule Castが自分の表示Entityへ衝突する自己衝突を避けます。
//
// 現段階の標準Gamepad操作:
//   Left Stick : World XZ平面を移動
//   A          : Jump
//   RT         : Run
//
// Camera-relative movement / Right Stick Cameraは次段階で入力方向変換として追加します。
class CharacterControllerDemoLayer final : public Layer
{
public:
    explicit CharacterControllerDemoLayer(Application& application)
        : m_Application(application)
    {
    }

    void OnAttach() override;
    void OnDetach() override;
    void OnUpdate(float deltaTime) override;

private:
    // CharacterControllerが更新するTransformは「足元Root」です。
    // 表示用Cubeは原点中心なので、RootからCapsule全高の半分だけ上へずらして同期します。
    void SyncVisualTransform();

private:
    Application& m_Application;

    CharacterController m_CharacterController{};
    TransformComponent m_CharacterRootTransform{};

    Entity m_CharacterEntity{};
    Ref<Mesh> m_CharacterMesh;
    Ref<Material> m_CharacterMaterial;
};

} // namespace Raven
