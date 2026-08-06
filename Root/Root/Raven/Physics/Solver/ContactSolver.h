#pragma once

#include "Raven/Physics/Contact.h"

namespace Raven
{

class Scene;

namespace ph
{

// ============================================================================
// ContactSolver
// ============================================================================
// Contactに記録された貫通量・法線・Material情報を使って、位置と速度を補正します。
//
// 現段階では1つのContactを1回だけ解決する最小Solverです。
// 将来、複数接触・積み重ね・回転を安定させる段階でSequential Impulseへ拡張します。
void SolveContact(Scene& scene, const Contact& contact, float dt);

} // namespace ph

} // namespace Raven
