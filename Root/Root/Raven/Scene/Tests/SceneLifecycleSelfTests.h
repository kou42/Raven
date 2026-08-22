#pragma once

namespace Raven::tests
{

// Scene終了時の残存Entity回収と、OnDestroy()多重呼び出しの安全性を確認します。
void RunSceneLifecycleSelfTests();

} // namespace Raven::tests
