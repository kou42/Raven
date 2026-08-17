#pragma once

#include <cstdint>

namespace Raven
{
namespace ph
{

// 2つのParticle間距離をRestLengthへ保つXPBD距離制約です。
// Clothでは縦横や対角線のParticleを接続する基本制約として使用します。
struct XPBDDistanceConstraint
{
    uint32_t ParticleA = 0u;
    uint32_t ParticleB = 0u;
    float RestLength = 0.0f;

    // 0に近いほど硬く、大きいほど柔らかい制約になります。
    // XPBDではComplianceを使うことでdtへの依存を抑えます。
    float Compliance = 0.0f;

    // 同一Step内の反復で使用するLagrange multiplierです。
    float Lambda = 0.0f;
};

} // namespace ph
} // namespace Raven
