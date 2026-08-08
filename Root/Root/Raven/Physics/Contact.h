#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "Raven/Math/MathVector.h"
#include "Raven/Scene/Entity.h"

namespace Raven::ph
{

// ContactFeatureType は、衝突に関与した幾何学的特徴の種類を表します。
// Narrow Phase では、この値を使って接触が face、edge、vertex のどれに由来するかを識別します。
// さらに Persistence では、古い接触情報を再利用できるかどうかを判断するために比較されます。
enum class ContactFeatureType : uint8_t
{
    Unknown = 0,
    Face = 1,
    Edge = 2,
    Vertex = 3
};

// ContactFeatureID は、衝突に関与する各ボディの特徴情報を保持します。
// 2 つのボディのどちらも意味のある特徴タイプを持っている場合にのみ、接触は有効と見なされます。
struct ContactFeatureID
{
    ContactFeatureType TypeA = ContactFeatureType::Unknown;
    ContactFeatureType TypeB = ContactFeatureType::Unknown;

    // 各ボディ上の特徴インデックスです。
    // たとえばボックス衝突形状では、face / edge / vertex のインデックスが形状ごとのルールで符号化されます。
    uint8_t IndexA = 0xFF;
    uint8_t IndexB = 0xFF;

    bool IsValid() const
    {
        return TypeA != ContactFeatureType::Unknown && TypeB != ContactFeatureType::Unknown;
    }

    bool operator==(const ContactFeatureID& other) const
    {
        return TypeA == other.TypeA &&
               TypeB == other.TypeB &&
               IndexA == other.IndexA &&
               IndexB == other.IndexB;
    }
};

// ContactPoint は、接触マニホールド内の 1 つの衝突点を表します。
// 1 つのマニホールドに複数の点を持たせることで、ボックスが辺に沿って接触するような複雑な形状でも扱いやすくなります。
struct ContactPoint
{
    // この接触点のワールド空間上の位置です。
    math::Vec3 Position{0.0f, 0.0f, 0.0f};
    float Penetration = 0.0f;

    // ソルバーの反復計算中に蓄積されたインパルスです。
    // 法線方向のインパルスは 2 つのボディを押し離し、接線方向のインパルスは滑り抵抗を表します。
    float AccumulatedNormalImpulse = 0.0f;
    float AccumulatedTangentImpulse = 0.0f;
    math::Vec3 CachedTangent{0.0f, 0.0f, 0.0f};

    // Narrow Phase で得られた幾何学的特徴情報です。
    // これは Persistence でワールド空間座標を比較する前に保持されます。
    ContactFeatureID Feature{};

    // ローカル空間上のアンカーは、ソルバーの反復計算中に接触位置を安定して保持するために使われます。
    // 特にボディの局所座標系に対して接触点を計算する必要がある場合に有効です。
    math::Vec3 LocalAnchorA{0.0f, 0.0f, 0.0f};
    math::Vec3 LocalAnchorB{0.0f, 0.0f, 0.0f};
    float InitialSeparation = 0.0f;
    bool PositionAnchorsInitialized = false;
};

// ContactManifold は、1 対のエンティティに対する複数の接触点をまとめた構造体です。
// 衝突解決のためにソルバーへ渡される主要なデータ構造として使われます。
struct ContactManifold
{
    // 1 つのマニホールドに格納できる接触点の最大数です。
    static constexpr std::size_t MaxContactPointCount = 4;

    // この接触セットに含まれる 2 つのエンティティです。
    Entity A;
    Entity B;

    // ワールド空間上の衝突法線です。
    // 多くのエンジンでは、この法線は body B から body A を向くように設定され、
    // 接触制約を解く際に 2 つのボディを分離する方向としてそのまま使えます。
    math::Vec3 Normal{0.0f, 1.0f, 0.0f};

    // ソルバーが衝突応答計算に用いる物理パラメータです。
    float Restitution = 0.0f;
    float StaticFriction = 0.0f;
    float DynamicFriction = 0.0f;

    // true の場合、この接触は実体の衝突制約ではなく、トリガー領域の重なりとして扱われます。
    bool IsTrigger = false;

    // このボディペアに対して収集された接触点の配列です。
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

        Points[PointCount++] = point;
        return true;
    }
};

// Contact は、マニホールドへ変換される前の軽量な一時的な接触情報です。
// 衝突検出やマニホールド生成の入力として使われる、シンプルな構造体です。
struct Contact
{
    Entity A;
    Entity B;
    math::Vec3 Point{0.0f, 0.0f, 0.0f};
    math::Vec3 Normal{0.0f, 1.0f, 0.0f};
    float Penetration = 0.0f;
    float Restitution = 0.0f;
    float StaticFriction = 0.0f;
    float DynamicFriction = 0.0f;
    bool IsTrigger = false;
};

// 単一の Contact からマニホールドを作成します。
// 共通の物理情報をコピーし、接触点をマニホールドの先頭に格納することで、
// ソルバーに渡す構造を統一します。
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
