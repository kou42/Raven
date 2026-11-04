#pragma once

namespace Raven::tests
{
void RunSceneGraphSelfTests();
// Debug/Releaseの両方で同じ負荷を測り、Window/GPUを生成せず終了します。
void RunSceneGraphMeasurements();
}
