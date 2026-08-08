#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "Raven/Math/MathVector.h"
#include "Raven/Scene/Entity.h"

namespace Raven::ph
{

enum class ContactFeatureType : uint8_t { Unknown=0, Face=1, Edge=2, Vertex=3 };

struct ContactFeatureID
{
    ContactFeatureType TypeA=ContactFeatureType::Unknown;
    ContactFeatureType TypeB=ContactFeatureType::Unknown;
    uint8_t IndexA=0xFF;
    uint8_t IndexB=0xFF;
    bool IsValid()const{return TypeA!=ContactFeatureType::Unknown&&TypeB!=ContactFeatureType::Unknown;}
    bool operator==(const ContactFeatureID&o)const{return TypeA==o.TypeA&&TypeB==o.TypeB&&IndexA==o.IndexA&&IndexB==o.IndexB;}
};

struct ContactPoint
{
    math::Vec3 Position{0,0,0};float Penetration=0;
    float AccumulatedNormalImpulse=0;float AccumulatedTangentImpulse=0;math::Vec3 CachedTangent{0,0,0};

    // Narrow Phaseが決めた幾何feature。Persistenceではworld座標より先に比較します。
    // IndexはBoxではface=axis*2+sign、edge=axis*4+side bits、vertex=3-bit cornerです。
    ContactFeatureID Feature{};

    math::Vec3 LocalAnchorA{0,0,0};math::Vec3 LocalAnchorB{0,0,0};float InitialSeparation=0;bool PositionAnchorsInitialized=false;
};

struct ContactManifold
{
    static constexpr std::size_t MaxContactPointCount=4;Entity A;Entity B;math::Vec3 Normal{0,1,0};float Restitution=0;float StaticFriction=0;float DynamicFriction=0;bool IsTrigger=false;std::array<ContactPoint,MaxContactPointCount> Points{};std::size_t PointCount=0;
    void ClearPoints(){PointCount=0;}bool AddPoint(const ContactPoint&p){if(PointCount>=MaxContactPointCount)return false;Points[PointCount++]=p;return true;}
};
struct Contact{Entity A;Entity B;math::Vec3 Point{0,0,0};math::Vec3 Normal{0,1,0};float Penetration=0;float Restitution=0;float StaticFriction=0;float DynamicFriction=0;bool IsTrigger=false;};
inline ContactManifold MakeContactManifold(const Contact&c){ContactManifold m{};m.A=c.A;m.B=c.B;m.Normal=c.Normal;m.Restitution=c.Restitution;m.StaticFriction=c.StaticFriction;m.DynamicFriction=c.DynamicFriction;m.IsTrigger=c.IsTrigger;ContactPoint p{};p.Position=c.Point;p.Penetration=c.Penetration;m.AddPoint(p);return m;}
} // namespace Raven::ph
