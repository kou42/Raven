#pragma once

namespace Raven::tests
{
// GPU/Windowを生成せず、OpenGL Frame失敗後の復帰とSafety Check、
// Explicit SceneのFrame順序・Pipeline入力・Asset境界を検証します。
void RunExplicitSceneFrameSelfTests();
} // namespace Raven::tests
