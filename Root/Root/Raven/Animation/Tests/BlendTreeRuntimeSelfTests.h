// Raven/Animation/Tests/BlendTreeRuntimeSelfTests.h
#pragma once

namespace Raven::tests
{

// 1D Blend TreeのWeight解決と、Speedを0 -> 2 -> 6へ連続変化させた際の
// StateMachine / Animator Runtime同期をassertで検証します。
void RunBlendTreeRuntimeSelfTests();

} // namespace Raven::tests
