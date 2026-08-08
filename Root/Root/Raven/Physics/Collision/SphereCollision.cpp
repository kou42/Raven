#include <algorithm>
#include <cmath>

#include "Raven/Physics/Collision/CollisionDetection.h"
#include "Raven/Physics/Collision/OBB.h"

namespace Raven::ph
{
namespace
{
void SetCombinedMaterial(
    const ColliderComponent& colliderA,
    const ColliderComponent& colliderB,
    ContactManifold& manifold)
{
    manifold.Restitution = std::min(std::max(colliderA.Restitution, 0.0f), std::max(colliderB.Restitution, 0.0f));
    manifold.StaticFriction = std::sqrt(std::max(colliderA.StaticFriction, 0.0f) * std::max(colliderB.StaticFriction, 0.0f));
    manifold.DynamicFriction = std::sqrt(std::max(colliderA.DynamicFriction, 0.0f) * std::max(colliderB.DynamicFriction, 0.0f));
    manifold.IsTrigger = colliderA.IsTrigger || colliderB.IsTrigger;
}
}

bool GenerateSphereSphereManifold(Entity a,const TransformComponent& ta,const ColliderComponent& ca,Entity b,const TransformComponent& tb,const ColliderComponent& cb,ContactManifold& out)
{
    if(ca.Type!=ColliderType::Sphere||cb.Type!=ColliderType::Sphere||ca.Radius<=0.0f||cb.Radius<=0.0f)return false;
    const math::Vec3 pa=ta.Position+ca.Offset,pb=tb.Position+cb.Offset,d=pb-pa; const float ds=d.LengthSq(),rs=ca.Radius+cb.Radius; if(ds>rs*rs)return false;
    math::Vec3 n{1,0,0}; float dist=0; if(ds>1e-12f){dist=std::sqrt(ds);n=d/dist;}
    ContactPoint p{};p.Position=((pa+n*ca.Radius)+(pb-n*cb.Radius))*0.5f;p.Penetration=rs-dist;
    out={};out.A=a;out.B=b;out.Normal=n;SetCombinedMaterial(ca,cb,out);out.AddPoint(p);return true;
}

bool GenerateSpherePlaneManifold(Entity a,const TransformComponent& ta,const ColliderComponent& ca,Entity b,const TransformComponent& tb,const ColliderComponent& cb,ContactManifold& out)
{
    if(ca.Type!=ColliderType::Sphere||cb.Type!=ColliderType::Plane||ca.Radius<=0.0f)return false;
    const float ls=cb.PlaneNormal.LengthSq();if(ls<=1e-12f)return false;const math::Vec3 n=cb.PlaneNormal/std::sqrt(ls),c=ta.Position+ca.Offset,p0=tb.Position+cb.Offset+n*cb.PlaneOffset;const float sd=math::Vec3::Dot(c-p0,n);if(sd>ca.Radius)return false;
    ContactPoint p{};p.Position=c-n*sd;p.Penetration=ca.Radius-sd;out={};out.A=a;out.B=b;out.Normal=-n;SetCombinedMaterial(ca,cb,out);out.AddPoint(p);return true;
}

bool GenerateBoxPlaneManifold(
    Entity boxEntity,
    const TransformComponent& boxTransform,
    const ColliderComponent& boxCollider,
    Entity planeEntity,
    const TransformComponent& planeTransform,
    const ColliderComponent& planeCollider,
    ContactManifold& outManifold)
{
    if (boxCollider.Type != ColliderType::Box || planeCollider.Type != ColliderType::Plane)
        return false;

    const float normalLengthSquared = planeCollider.PlaneNormal.LengthSq();
    if (normalLengthSquared <= 1.0e-12f)
        return false;

    OBB box{};
    if (!ComputeBoxOBB(boxTransform, boxCollider, box))
        return false;

    const math::Vec3 planeNormal = planeCollider.PlaneNormal / std::sqrt(normalLengthSquared);
    const math::Vec3 pointOnPlane = planeTransform.Position + planeCollider.Offset + planeNormal * planeCollider.PlaneOffset;

    outManifold = ContactManifold{};
    outManifold.A = boxEntity;
    outManifold.B = planeEntity;
    // ContactSolverがAとBを互いに分離できるよう、既存Sphere-Planeと同じ
    // A(Box) -> B(Plane) 規約で法線を設定します。
    outManifold.Normal = -planeNormal;
    SetCombinedMaterial(boxCollider, planeCollider, outManifold);

    // OBBの8頂点を生成します。各頂点の符号組み合わせを使うことで、
    // Transform::Rotationを含むワールド空間上の正しい角位置を直接得られます。
    for (int vertexIndex = 0; vertexIndex < 8; ++vertexIndex)
    {
        const float sx = (vertexIndex & 1) ? 1.0f : -1.0f;
        const float sy = (vertexIndex & 2) ? 1.0f : -1.0f;
        const float sz = (vertexIndex & 4) ? 1.0f : -1.0f;

        const math::Vec3 vertex = box.Center
            + box.Axis[0] * (box.HalfExtents.x * sx)
            + box.Axis[1] * (box.HalfExtents.y * sy)
            + box.Axis[2] * (box.HalfExtents.z * sz);

        const float signedDistance = math::Vec3::Dot(vertex - pointOnPlane, planeNormal);
        if (signedDistance > 0.0f)
            continue;

        ContactPoint point{};
        // 接触点はBox頂点そのものではなく平面上へ射影します。
        // これによりSolverのlever armが実際の支持面上に置かれ、回転応答が安定します。
        point.Position = vertex - planeNormal * signedDistance;
        point.Penetration = -signedDistance;
        point.Feature.TypeA = ContactFeatureType::Vertex;
        point.Feature.IndexA = static_cast<uint8_t>(vertexIndex);
        point.Feature.TypeB = ContactFeatureType::Face;
        point.Feature.IndexB = 0;
        outManifold.AddPoint(point);
    }

    return outManifold.PointCount > 0;
}

bool GenerateSphereBoxManifold(Entity a,const TransformComponent& ta,const ColliderComponent& ca,Entity b,const TransformComponent& tb,const ColliderComponent& cb,ContactManifold& out)
{
    if(ca.Type!=ColliderType::Sphere||cb.Type!=ColliderType::Box||ca.Radius<=0.0f)return false;OBB box{};if(!ComputeBoxOBB(tb,cb,box))return false;const math::Vec3 c=ta.Position+ca.Offset,lc=box.ToLocalPoint(c);const math::Vec3 q{std::clamp(lc.x,-box.HalfExtents.x,box.HalfExtents.x),std::clamp(lc.y,-box.HalfExtents.y,box.HalfExtents.y),std::clamp(lc.z,-box.HalfExtents.z,box.HalfExtents.z)};math::Vec3 cp=box.ToWorldPoint(q),d=cp-c;const float ds=d.LengthSq();if(ds>ca.Radius*ca.Radius)return false;math::Vec3 n{};float pen=0;if(ds>1e-12f){const float dist=std::sqrt(ds);n=d/dist;pen=ca.Radius-dist;}else{int axis=0;float sign=lc.x>=0?1.0f:-1.0f,fd=box.HalfExtents.x-std::abs(lc.x);for(int i=1;i<3;++i){float cd=box.HalfExtents[i]-std::abs(lc[i]);if(cd<fd){axis=i;sign=lc[i]>=0?1.0f:-1.0f;fd=cd;}}math::Vec3 lcp=lc;lcp[axis]=box.HalfExtents[axis]*sign;cp=box.ToWorldPoint(lcp);n=-(box.Axis[axis]*sign);pen=ca.Radius+fd;}ContactPoint p{};p.Position=cp;p.Penetration=std::max(pen,0.0f);out={};out.A=a;out.B=b;out.Normal=n;SetCombinedMaterial(ca,cb,out);out.AddPoint(p);return true;
}

} // namespace Raven::ph
