#pragma once

#include <cstdint>
#include <vector>

#include "Raven/Physics/Contact.h"

namespace Raven
{
class Scene;

namespace ph
{

struct ContactSolverSettings
{
    // 速度拘束の反復回数。大きいほど反発/摩擦の収束が良くなります。
    uint32_t VelocityIterations = 8;

    // Position Solverも反復させます。Velocity Solverとは分離し、速度へ人工的な
    // エネルギーを注入せずにpenetrationだけを解消します。
    uint32_t PositionIterations = 3;
    // この値未満の微小貫通は許容し、ジッターを抑えます。
    float PenetrationSlop = 0.001f;
    // 1反復で補正する割合。1.0に近いほど強く押し戻します。
    float PositionCorrectionPercent = 0.8f;

    // この速度以下の衝突では反発を無効化し、細かな振動を防ぎます。
    float RestitutionVelocityThreshold = 0.5f;

    // Contact Persistenceで前Stepから引き継いだ累積Impulseを、反復Solver開始前に
    // 初期解としてBodyへ適用します。積み重ね・静止接触の収束を高速化します。
    bool EnableWarmStart = true;
};

// マニホールド配列全体を解くメイン入口です。
// 1 step内で WarmStart -> Velocity Iteration -> Position Iteration を実行します。
void SolveContactManifolds(
    Scene& scene,
    std::vector<ContactManifold>& manifolds,
    float dt,
    const ContactSolverSettings& settings = ContactSolverSettings{});

// 単一マニホールド版のユーティリティ。主にテスト/デバッグ用途です。
void SolveContactManifold(
    Scene& scene,
    ContactManifold& manifold,
    float dt);

} // namespace ph
} // namespace Raven
