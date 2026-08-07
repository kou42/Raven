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
// Sphere系では1点、Box-Boxでは最大4点を保持します。
struct ContactPoint
{
    math::Vec3 Position{ 0.0f, 0.0f, 0.0f };
    float Penetration = 0.0f;

    // Sequential Impulseで解いた累積Impulseです。
    // Contact Persistenceによって次フレームの対応Contactへ引き継ぎ、
    // Solver反復前にWarm Startとして再適用します。
    float AccumulatedNormalImpulse = 0.0f;
    float AccumulatedTangentImpulse = 0.0f;

    // 摩擦Impulseはスカラー値だけでは方向を復元できないため、前回Solverで使った
    // 接線基底も保存します。Warm Startでは
    //   Normal * NormalImpulse + CachedTangent * TangentImpulse
    // をまとめて再適用します。
    math::Vec3 CachedTangent{ 0.0f, 0.0f, 0.0f };
};

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

// 旧来の単一接触表現です。
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
