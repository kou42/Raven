#pragma once

namespace Raven::ph::tests
{

bool RunElectromagneticGridSelfTests();

// クーロン力の向き・距離依存・作用反作用をassertで確認します。
void RunElectromagnetismSelfTests();

} // namespace Raven::ph::tests
