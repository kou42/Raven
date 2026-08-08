#pragma once

#include <algorithm>
#include <cmath>

#include "Raven/Math/MathUtility.h"
#include "Raven/Scene/Components.h"

namespace Raven::ph
{

struct OBB
{
    math::Vec3 Center{};
    math::Vec3 HalfExtents{ 0.5f, 0.5f, 0.5f };
    math::Vec3 Axis[3]{
        math::Vec3{ 1.0f, 0.0f, 0.0f },
        math::Vec3{ 0.0f, 1.0f, 0.0f },
        math::Vec3{ 0.0f, 0.0f, 1.0f }
    };

    math::Vec3 ToWorldPoint(const math::Vec3& localPoint) const
    {
        return Center + Axis[0] * localPoint.x + Axis[1] * localPoint.y + Axis[2] * localPoint.z;
    }

    math::Vec3 ToLocalPoint(const math::Vec3& worldPoint) const
    {
        const math::Vec3 delta = worldPoint - Center;
        return { math::Vec3::Dot(delta, Axis[0]), math::Vec3::Dot(delta, Axis[1]),
            math::Vec3::Dot(delta, Axis[2]) };
    }

    math::Vec3 ToLocalVector(const math::Vec3& worldVector) const
    {
        return { math::Vec3::Dot(worldVector, Axis[0]), math::Vec3::Dot(worldVector, Axis[1]),
            math::Vec3::Dot(worldVector, Axis[2]) };
    }

    math::Vec3 Support(const math::Vec3& direction) const
    {
        math::Vec3 point = Center;
        for (int axis = 0; axis < 3; ++axis)
        {
            const float sign = math::Vec3::Dot(direction, Axis[axis]) >= 0.0f ? 1.0f : -1.0f;
            point += Axis[axis] * (HalfExtents[axis] * sign);
        }
        return point;
    }

    // RayをOBBローカル空間へ変換すると、OBBは原点中心のAABBになります。
    // そのため3軸Slab法をそのまま使えます。返すfractionはworld Rayの
    // origin + direction * fraction と同じパラメータです。
    bool RayCast(const math::Vec3& origin, const math::Vec3& direction, float maxFraction,
        float& outFraction, math::Vec3* outNormal = nullptr) const
    {
        constexpr float parallelEpsilon = 1.0e-8f;
        const math::Vec3 localOrigin = ToLocalPoint(origin);
        const math::Vec3 localDirection = ToLocalVector(direction);
        float tMin = 0.0f;
        float tMax = std::max(maxFraction, 0.0f);
        int hitAxis = -1;
        float hitSign = 0.0f;

        for (int axis = 0; axis < 3; ++axis)
        {
            const float o = localOrigin[axis];
            const float d = localDirection[axis];
            const float extent = HalfExtents[axis];
            if (std::abs(d) <= parallelEpsilon)
            {
                if (o < -extent || o > extent) return false;
                continue;
            }

            const float invD = 1.0f / d;
            float t1 = (-extent - o) * invD;
            float t2 = ( extent - o) * invD;
            float normalSign = -1.0f;
            if (t1 > t2)
            {
                std::swap(t1, t2);
                normalSign = 1.0f;
            }
            if (t1 > tMin)
            {
                tMin = t1;
                hitAxis = axis;
                hitSign = normalSign;
            }
            tMax = std::min(tMax, t2);
            if (tMin > tMax) return false;
        }

        outFraction = tMin;
        if (outNormal)
        {
            if (hitAxis >= 0) *outNormal = Axis[hitAxis] * hitSign;
            else *outNormal = -direction.Normalized(); // Ray originがOBB内部の場合
        }
        return true;
    }
};

inline bool ComputeBoxOBB(const TransformComponent& transform, const ColliderComponent& collider,
    OBB& outOBB)
{
    if (collider.Type != ColliderType::Box) return false;

    const math::Vec3 halfExtents{ std::abs(collider.HalfExtents.x), std::abs(collider.HalfExtents.y),
        std::abs(collider.HalfExtents.z) };
    if (halfExtents.x <= 0.0f || halfExtents.y <= 0.0f || halfExtents.z <= 0.0f) return false;

    math::Mat4 rotation = math::Mat4::Identity();
    rotation = math::Rotate(rotation, transform.Rotation.x, math::Vec3{ 1.0f, 0.0f, 0.0f });
    rotation = math::Rotate(rotation, transform.Rotation.y, math::Vec3{ 0.0f, 1.0f, 0.0f });
    rotation = math::Rotate(rotation, transform.Rotation.z, math::Vec3{ 0.0f, 0.0f, 1.0f });

    outOBB.Axis[0] = math::Vec3{ rotation[0][0], rotation[1][0], rotation[2][0] }.Normalized();
    outOBB.Axis[1] = math::Vec3{ rotation[0][1], rotation[1][1], rotation[2][1] }.Normalized();
    outOBB.Axis[2] = math::Vec3{ rotation[0][2], rotation[1][2], rotation[2][2] }.Normalized();
    outOBB.HalfExtents = halfExtents;
    outOBB.Center = transform.Position + outOBB.Axis[0] * collider.Offset.x
        + outOBB.Axis[1] * collider.Offset.y + outOBB.Axis[2] * collider.Offset.z;
    return true;
}

} // namespace Raven::ph
