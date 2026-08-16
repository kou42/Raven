// Raven/Character/CharacterController.h
#pragma once

#include <cstddef>
#include <string>

#include "Raven/Math/MathVector.h"
#include "Raven/Scene/Components.h"

namespace Raven
{
namespace Gltf
{
class SkinnedBlendTreeRuntime;
}

// ============================================================================
// CharacterControllerInput
// ============================================================================
// Character Controllerが必要とする入力をDevice非依存の値へ変換した構造体です。
// Keyboard / GamepadをController内部で直接読むと、将来Input MappingやAI操作へ切り替える際に
// 移動ロジックまで変更する必要が出るため、入力取得と運動計算を明確に分離します。
struct CharacterControllerInput
{
    // X: Right(+1) / Left(-1)
    // Y: Forward(+1) / Backward(-1)
    math::Vec2 Move{ 0.0f, 0.0f };

    bool Run = false;
    bool Jump = false;
};

// ============================================================================
// CharacterControllerConfig
// ============================================================================
struct CharacterControllerConfig
{
    float WalkSpeed = 1.8f;
    float RunSpeed = 5.5f;

    // 目標速度へ近付く水平加速度です。
    float Acceleration = 14.0f;
    float Deceleration = 18.0f;

    // rad/s。0以下なら向きを瞬時に移動方向へ合わせます。
    float TurnSpeed = 10.0f;

    float Gravity = -9.81f;
    float JumpSpeed = 4.5f;

    // Stage 5の最小Ground判定です。
    // Transform::Position.yがこの高さ以下へ到達したらGroundedとしてClampします。
    // 後続ではPhysicsWorldのSweep / Contact Queryへ置き換える境界です。
    float GroundHeight = 0.0f;
};

// ============================================================================
// CharacterController
// ============================================================================
// Stage 5のKinematic Character Controllerです。
//
// Update順:
//   Input -> Desired Horizontal Velocity
//         -> Acceleration / Deceleration
//         -> Facing Rotation
//         -> Gravity / Grounded / Jump
//         -> Transform Position
//
// Character自身をDynamic RigidBodyにすると入力移動とImpulse Solverが同じ自由度を奪い合うため、
// 現段階ではゲームロジックが位置を決定するKinematic Controllerとして実装します。
class CharacterController
{
public:
    CharacterController() = default;
    explicit CharacterController(const CharacterControllerConfig& config)
        : m_Config(config)
    {
    }

    void SetConfig(const CharacterControllerConfig& config) { m_Config = config; }
    const CharacterControllerConfig& GetConfig() const { return m_Config; }

    // 毎Frameの移動更新です。
    // TransformComponentを直接更新し、成功時trueを返します。
    bool Update(
        const CharacterControllerInput& input,
        float deltaTime,
        TransformComponent& transform,
        std::string* errorMessage = nullptr);

    // Raven標準Keyboard入力(WASD / Left Shift / Space)をDevice非依存入力へ変換します。
    // Input Mapping System導入後はこの関数だけを置き換え、運動計算は維持できます。
    static CharacterControllerInput ReadDefaultKeyboardInput();

    // Stage 4 BlendTreeとの接続用Helperです。
    // Controller自身はAnimation Runtimeを所有せず、実際の水平速度だけをParameterとして渡します。
    bool UpdateLocomotionAnimation(
        Gltf::SkinnedBlendTreeRuntime& animationRuntime,
        std::size_t skinIndex,
        std::string* errorMessage = nullptr) const;

    const math::Vec3& GetVelocity() const { return m_Velocity; }

    float GetHorizontalSpeed() const;

    bool IsGrounded() const { return m_Grounded; }

private:
    bool ValidateConfig(std::string* errorMessage) const;

private:
    CharacterControllerConfig m_Config{};
    math::Vec3 m_Velocity{ 0.0f, 0.0f, 0.0f };
    bool m_Grounded = false;
};

} // namespace Raven
