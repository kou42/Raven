#include <algorithm>
#include <cmath>

#include "Raven/Physics/Collision/CollisionDetection.h"

namespace Raven::ph
{

bool GenerateSpherePlaneContact(
    Entity sphereEntity,
    const TransformComponent& sphereTransform,
    const ColliderComponent& sphereCollider,
    Entity planeEntity,
    const TransformComponent& planeTransform,
    const ColliderComponent& planeCollider,
    Contact& outContact
)
{
    // ========================================================================
    // 1. Collider種別の確認
    // ========================================================================
    // 間違ったColliderの組み合わせでこの関数が呼ばれても、未定義のパラメータを
    // 使用しないように防御的に確認します。
    if (sphereCollider.Type != ColliderType::Sphere
        || planeCollider.Type != ColliderType::Plane)
    {
        return false;
    }

    // 半径が0以下のSphereは体積を持たず、通常の球として扱えません。
    // 負の半径をそのまま利用すると貫通量の符号まで壊れるため除外します。
    const float sphereRadius = sphereCollider.Radius;
    if (sphereRadius <= 0.0f)
    {
        return false;
    }

    // ========================================================================
    // 2. Plane法線の正規化
    // ========================================================================
    // 点と平面の距離公式
    //
    //     signedDistance = dot(point - pointOnPlane, normal)
    //
    // はnormalの長さが1であることを前提とします。
    // normalの長さが2なら距離も2倍になってしまうため、必ず正規化します。
    const float planeNormalLengthSquared = planeCollider.PlaneNormal.LengthSq();

    // ゼロベクトルは向きを持たないため、平面を定義できません。
    constexpr float NormalEpsilonSquared = 1.0e-12f;
    if (planeNormalLengthSquared <= NormalEpsilonSquared)
    {
        return false;
    }

    const math::Vec3 planeNormal =
        planeCollider.PlaneNormal / std::sqrt(planeNormalLengthSquared);

    // ========================================================================
    // 3. ワールド空間上のSphere中心とPlane上の基準点を求める
    // ========================================================================
    // 現段階ではCollider::Offsetをワールド軸に沿ったオフセットとして扱います。
    // 回転運動導入後は、Transformの回転でOffsetを回してから加算します。
    const math::Vec3 sphereCenter = sphereTransform.Position + sphereCollider.Offset;

    // PlaneOffsetは法線方向への追加距離です。
    // 例えばTransform.Position=(0,0,0), Normal=(0,1,0), PlaneOffset=2なら
    // Y=2の水平面になります。
    const math::Vec3 pointOnPlane =
        planeTransform.Position
        + planeCollider.Offset
        + planeNormal * planeCollider.PlaneOffset;

    // ========================================================================
    // 4. Sphere中心からPlaneまでの符号付き距離
    // ========================================================================
    //
    //     signedDistance = dot(sphereCenter - pointOnPlane, planeNormal)
    //
    // signedDistance > 0 : Sphere中心はPlane法線側
    // signedDistance = 0 : Sphere中心はPlane上
    // signedDistance < 0 : Sphere中心はPlaneの裏側
    //
    // RavenのPlaneは「法線側を空間、反対側を固体」とする片面Planeです。
    // したがって、中心が裏側へ入り込んだ場合も接触として扱い、表側へ押し戻します。
    const float signedDistance =
        math::Vec3::Dot(sphereCenter - pointOnPlane, planeNormal);

    // Sphereの最もPlaneに近い表面までの距離は
    //
    //     signedDistance - radius
    //
    // です。これが正なら隙間があり、0以下なら接触または貫通しています。
    if (signedDistance > sphereRadius)
    {
        return false;
    }

    // ========================================================================
    // 5. Contact情報の生成
    // ========================================================================
    // 貫通量は、SphereをPlane法線方向へ何m戻せば接触状態へ戻るかを表します。
    //
    //     penetration = radius - signedDistance
    //
    // 接しているだけなら0、深く入り込むほど大きな正値になります。
    const float penetration = sphereRadius - signedDistance;

    // Sphere中心をPlaneへ正射影した点です。
    // signedDistanceが負の場合も、この式でPlane上の点を正しく取得できます。
    const math::Vec3 projectedPoint =
        sphereCenter - planeNormal * signedDistance;

    outContact.A = sphereEntity;
    outContact.B = planeEntity;
    outContact.Point = projectedPoint;

    // Contact::NormalはAからBへ向く規約です。
    // AはSphere、BはPlaneなので、SphereからPlaneへ向く方向は-planeNormalです。
    outContact.Normal = -planeNormal;
    outContact.Penetration = penetration;

    // 2つのMaterial値を組み合わせます。
    // Restitutionは極端に弾む側だけに引っ張られないよう小さい方を採用します。
    // 摩擦係数は幾何平均を使い、片方が0なら接触全体も滑りやすくなります。
    outContact.Restitution = std::min(
        std::max(sphereCollider.Restitution, 0.0f),
        std::max(planeCollider.Restitution, 0.0f)
    );

    outContact.StaticFriction = std::sqrt(
        std::max(sphereCollider.StaticFriction, 0.0f)
        * std::max(planeCollider.StaticFriction, 0.0f)
    );

    outContact.DynamicFriction = std::sqrt(
        std::max(sphereCollider.DynamicFriction, 0.0f)
        * std::max(planeCollider.DynamicFriction, 0.0f)
    );

    outContact.IsTrigger = sphereCollider.IsTrigger || planeCollider.IsTrigger;

    return true;
}

} // namespace Raven::ph
