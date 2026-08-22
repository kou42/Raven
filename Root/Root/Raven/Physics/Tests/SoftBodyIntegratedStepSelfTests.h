#pragma once

namespace Raven::ph::tests
{

// ============================================================================
// Integrated Soft Body Step Self Tests
// ============================================================================
// Cloth統合XPBD Step、Particle-Triangle Counter、Spatial Hash Cell Size比較を
// assertベースで検証するDebug向けSelfTestです。
//
// ゲームループから毎フレーム呼ぶのではなく、Debug起動時に1回だけ実行します。
// Releaseではmain.cpp側の _DEBUG ガードにより呼び出されません。
void RunSoftBodyIntegratedStepSelfTests();

} // namespace Raven::ph::tests
