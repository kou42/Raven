#pragma once

#include <array>
#include <cstddef>

#include "Raven/Math/MathVector.h"
#include "Raven/Scene/Entity.h"

namespace Raven::ph
{

// ============================================================================
// ContactPoint
// ============================================================================
// 1つの接触位置に属する局所的な情報です。
// Sphere同士では1点だけ使用しますが、Box-Boxでは最大4点程度を保持します。
struct ContactPoint
{
    math::Vec3 Position{ 0.0f, 0.0f, 0.0f };
    float Penetration = 0.0f;

    // 将来のSequential Impulse / Warm Startで使用する累積Impulseです。
    // 現在のSolverではまだ利用しませんが、Manifoldをフレーム間で永続化する際に
    // 前フレームの解を初期値として再利用できるよう、構造上の置き場所を確保します。
    float AccumulatedNormalImpulse = 0.0f;
    float AccumulatedTangentImpulse = 0.0f;
};

// ============================================================================
// ContactManifold
// ============================================================================
// 同じColliderペアに属する複数のContactPointをまとめる構造です。
// 法線・Material・Trigger属性はペア全体で共通とし、接触位置と貫通量だけを
// ContactPointごとに保持します。
struct ContactManifold
{
    static constexpr std::size_t MaxContactPointCount = 4;

    Entity A;
    Entity B;

    // AからBへ向く法線です。
    math::Vec3 Normal{ 0.0f, 1.0f, 0.0f };

    float Restitution = 0.0f;
    float StaticFriction = 0.0f;
    float DynamicFriction = 0.0f;
    bool IsTrigger = false;

    std::array<ContactPoint, MaxContactPointCount> Points{};
    std::size_t PointCount = 0;

    void ClearPoints()
    {
        PointCount = 0;
    }

    bool AddPoint(const ContactPoint& point)
    {
        if (PointCount >= MaxContactPointCount)
        {
            return false;
        }

        Points[PointCount] = point;
        ++PointCount;
        return true;
    }
};

// ============================================================================
// Contact
// ============================================================================
// 旧来の単一接触表現です。
// 段階移行中の互換性維持のため残していますが、新しい衝突検出・Solverは
// ContactManifoldを使用します。
struct Contact
{
    Entity A;
    Entity B;
    math::Vec3 Point{ 0.0f, 0.0f, 0.0f };
    math::Vec3 Normal{ 0.0f, 1.0f, 0.0f };
    float Penetration = 0.0f;
    float Restitution = 0.0f;
    float StaticFriction = 0.0f;
    float DynamicFriction = 0.0f;
    bool IsTrigger = false;
};

// 単一Contactを1点Manifoldへ変換します。
// Sphere-Sphere / Sphere-Planeの移行中に使用できます。
inline ContactManifold MakeContactManifold(const Contact& contact)
{
    ContactManifold manifold{};
    manifold.A = contact.A;
    manifold.B = contact.B;
    manifold.Normal = contact.Normal;
    manifold.Restitution = contact.Restitution;
    manifold.StaticFriction = contact.StaticFriction;
    manifold.DynamicFriction = contact.DynamicFriction;
    manifold.IsTrigger = contact.IsTrigger;

    ContactPoint point{};
    point.Position = contact.Point;
    point.Penetration = contact.Penetration;
    manifold.AddPoint(point);

    return manifold;
}

} // namespace Raven::ph
