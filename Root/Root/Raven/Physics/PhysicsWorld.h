#pragma once

#include <vector>

#include "Raven/Math/MathVector.h"

//Application
//実時間計測
//Scene更新
//ウィンドウ更新
//
//Scene
//EntityとComponentの所有
//PhysicsWorldの所有
//ゲームロジックと各Systemの実行順序管理
//
//RigidBodyComponent
//速度、質量、力、角速度などの物理データ
//
//ColliderComponent
//衝突形状とMaterial情報
//
//PhysicsWorld
//全剛体の積分
//衝突検出
//接触解決
//物理イベント生成
//
//Renderer
//Transformを読み取って描画

namespace Raven
{

class Scene;

namespace ph // 物理演算のための名前空間
{

class PhysicsWorld
{
public:
    void SetGravity(const math::Vec3& gravity);
    const math::Vec3& GetGravity() const;

    void Step(Scene& scene, float fixedDeltaTime);

private:
    void ApplyForces(Scene& scene, float dt);
    void IntegrateVelocities(Scene& scene, float dt);
    void DetectCollisions(Scene& scene);
    void SolveCollisions(Scene& scene, float dt);
    void IntegratePositions(Scene& scene, float dt);
    void ClearForces(Scene& scene);

    void AddForce(Scene& scene, Entity entity, const math::Vec3& force);

    void AddImpulse(Scene& scene, Entity entity, const math::Vec3& impulse);

private:
    math::Vec3 m_Gravity{ 0.0f, -9.80665f, 0.0f };
    //std::vector<Contact> m_Contacts;
};

} // end ph

} //  end Raven
