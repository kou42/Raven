#pragma once

#include "Raven/Physics/Contact.h"

namespace Raven
{

class Scene;

namespace ph
{

// ============================================================================
// ContactSolver
// ============================================================================
// Contactに記録された貫通量・法線・Material情報を使って、位置と速度を補正します。
//
// この関数は段階移行中の単一接触Solverです。
// ContactManifold版Solverからも、各ContactPointを解決するために内部利用します。
void SolveContact(Scene& scene, const Contact& contact, float dt);

// ============================================================================
// SolveContactManifold
// ============================================================================
// 同じColliderペアに属する複数のContactPointをまとめて解決します。
//
// 現在の段階では、Manifold内の各点を旧来のContactへ変換し、既存の
// SolveContact()へ順番に渡します。これによりSphere系の既存挙動を保ったまま、
// Box-Boxで必要になる複数接触点を受け取れるSolver APIへ移行できます。
//
// 将来のSequential Impulse化では、この関数を次の3段階へ発展させます。
//   1. Manifold全体のConstraintを事前計算する
//   2. 全接触点へWarm Startを適用する
//   3. 全Manifoldを複数回反復してImpulseを収束させる
inline void SolveContactManifold(
    Scene& scene,
    const ContactManifold& manifold,
    float dt)
{
    // 接触点を持たないManifoldは不正または非接触なので、何も解決しません。
    if (manifold.PointCount == 0)
    {
        return;
    }

    // PointCountはAddPoint()によって最大数以下に保たれますが、外部から値を直接
    // 変更された場合にも配列外参照しないよう、念のため上限を制限します。
    const std::size_t pointCount =
        manifold.PointCount < ContactManifold::MaxContactPointCount
        ? manifold.PointCount
        : ContactManifold::MaxContactPointCount;

    for (std::size_t pointIndex = 0; pointIndex < pointCount; ++pointIndex)
    {
        const ContactPoint& point = manifold.Points[pointIndex];

        Contact contact{};
        contact.A = manifold.A;
        contact.B = manifold.B;
        contact.Point = point.Position;
        contact.Normal = manifold.Normal;
        contact.Penetration = point.Penetration;
        contact.Restitution = manifold.Restitution;
        contact.StaticFriction = manifold.StaticFriction;
        contact.DynamicFriction = manifold.DynamicFriction;
        contact.IsTrigger = manifold.IsTrigger;

        SolveContact(scene, contact, dt);
    }
}

} // namespace ph

} // namespace Raven
