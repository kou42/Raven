#pragma once

#include "Raven/Math/MathVector.h"

namespace Raven
{

class Entity;
class Scene;

namespace ph // 物理演算のための名前空間
{

// ============================================================================
// PhysicsWorld
// ============================================================================
// Scene内に存在するRigidBodyComponentをまとめて更新する物理ワールドです。
//
// 第1段階では、次の責務を担当します。
//   1. 重力と蓄積された外力から加速度を計算する
//   2. 加速度から線形速度を更新する
//   3. 線形速度からTransformの位置を更新する
//   4. 速度減衰、力のクリア、簡易Sleep判定を行う
//   5. ゲーム側が剛体を安全に操作するためのAPIを提供する
//
// 衝突検出・衝突応答は後続段階で追加します。
// ただし処理順を後から大きく変更しなくてよいように、Step内には既に
// DetectCollisions / SolveCollisionsの呼び出し位置を確保しています。
class PhysicsWorld
{
public:
    void SetGravity(const math::Vec3& gravity);
    const math::Vec3& GetGravity() const;

    // 固定タイムステップ1回分の物理更新を行います。
    // Scene側から1/60秒など一定のfixedDeltaTimeを渡してください。
    void Step(Scene& scene, float fixedDeltaTime);

    // 継続的な力を加えます。
    // 例: エンジン推力、風、ばね、プレイヤーの押す力など。
    // Forceは次回Stepの終了時にクリアされるため、継続して作用させたい力は
    // ゲーム側から物理更新ごとに追加する必要があります。
    void AddForce(Scene& scene, Entity entity, const math::Vec3& force);

    // 瞬間的な力積を加えます。
    // 例: ジャンプ、爆発、衝突応答など。
    // 力積は時間積分を待たず、速度を deltaVelocity = impulse * inverseMass
    // だけ即座に変化させます。
    void AddImpulse(Scene& scene, Entity entity, const math::Vec3& impulse);

    // Torqueは回転運動におけるForceに相当します。
    // 第1段階では値の蓄積とWakeだけを行い、第4段階で慣性テンソルを通して
    // AngularVelocityへ反映する処理を追加します。
    void AddTorque(Scene& scene, Entity entity, const math::Vec3& torque);

    // 線形速度を直接設定します。
    // Dynamic Bodyを操作した場合はSleep状態を解除します。
    // Kinematic Bodyでは、ここで設定した速度を使って位置が更新されます。
    void SetLinearVelocity(Scene& scene, Entity entity, const math::Vec3& velocity);

    // RigidBodyを持たないEntityや無効なEntityではゼロベクトルを返します。
    math::Vec3 GetLinearVelocity(const Scene& scene, Entity entity) const;

    // Entityを指定位置へ瞬間移動します。
    // velocityを変更しないため、移動後も現在の速度を維持します。
    // 衝突Broad Phase導入後は、この関数内でAABBの再登録も行います。
    void Teleport(Scene& scene, Entity entity, const math::Vec3& position);

    // Kinematic Bodyを目標位置へ移動するための補助APIです。
    // 現段階ではTransformを直接更新します。
    // 将来は前回位置との差からKinematic速度を計算し、Dynamic Bodyへ
    // 正しい相対速度を伝えられるように拡張します。
    void MovePosition(Scene& scene, Entity entity, const math::Vec3& position);

    // Sleep中のBodyを明示的に起こします。
    // Transformをテレポートした場合や、ゲーム側で速度を直接変更した場合に使用します。
    void WakeUp(Scene& scene, Entity entity);

private:
    void ApplyForces(Scene& scene, float dt);
    void IntegrateVelocities(Scene& scene, float dt);
    void DetectCollisions(Scene& scene);
    void SolveCollisions(Scene& scene, float dt);
    void IntegratePositions(Scene& scene, float dt);
    void UpdateSleeping(Scene& scene, float dt);
    void ClearForces(Scene& scene);

private:
    // 地球表面付近の標準重力加速度。
    // Y軸を上方向とするため、重力は-Y方向です。
    math::Vec3 m_Gravity{ 0.0f, -9.80665f, 0.0f };
};

} // namespace ph

} // namespace Raven
