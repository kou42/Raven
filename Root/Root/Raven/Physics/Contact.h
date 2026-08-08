#pragma once

#include <array>
#include <cstddef>

#include "Raven/Math/MathVector.h"
#include "Raven/Scene/Entity.h"

namespace Raven::ph
{

struct ContactPoint
{
    math::Vec3 Position{ 0.0f, 0.0f, 0.0f };
    float Penetration = 0.0f;

    float AccumulatedNormalImpulse = 0.0f;
    float AccumulatedTangentImpulse = 0.0f;
    math::Vec3 CachedTangent{ 0.0f, 0.0f, 0.0f };

    // Position Solver用の接触アンカーです。
    // Narrow Phaseが生成したworld-space Positionを、Solver開始時に各Bodyの
    // 重心基準local-spaceへ保存します。Position/Orientation correction後は
    // このアンカーをworldへ戻すことで、古いContact Pointを固定したまま
    // penetrationを減算するのではなく、現在Poseからseparationを再評価できます。
    math::Vec3 LocalAnchorA{ 0.0f, 0.0f, 0.0f };
    math::Vec3 LocalAnchorB{ 0.0f, 0.0f, 0.0f };
    float InitialSeparation = 0.0f;
    bool PositionAnchorsInitialized = false;
};

struct ContactManifold
{
    static constexpr std::size_t MaxContactPointCount = 4;
    Entity A;
    Entity B;
    math::Vec3 Normal{ 0.0f, 1.0f, 0.0f };
    float Restitution = 0.0f;
    float StaticFriction = 0.0f;
    float DynamicFriction = 0.0f;
    bool IsTrigger = false;
    std::array<ContactPoint, MaxContactPointCount> Points{};
    std::size_t PointCount = 0;
    void ClearPoints(){PointCount=0;}
    bool AddPoint(const ContactPoint& point){if(PointCount>=MaxContactPointCount)return false;Points[PointCount]=point;++PointCount;return true;}
};

struct Contact
{
    Entity A;Entity B;math::Vec3 Point{0,0,0};math::Vec3 Normal{0,1,0};float Penetration=0;float Restitution=0;float StaticFriction=0;float DynamicFriction=0;bool IsTrigger=false;
};
inline ContactManifold MakeContactManifold(const Contact& contact){ContactManifold m{};m.A=contact.A;m.B=contact.B;m.Normal=contact.Normal;m.Restitution=contact.Restitution;m.StaticFriction=contact.StaticFriction;m.DynamicFriction=contact.DynamicFriction;m.IsTrigger=contact.IsTrigger;ContactPoint p{};p.Position=contact.Point;p.Penetration=contact.Penetration;m.AddPoint(p);return m;}

} // namespace Raven::ph
