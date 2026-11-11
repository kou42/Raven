#pragma once

namespace Raven::tests
{
void RunSceneViewSelfTests();
// Debug/Release共通のCPU計測入口。時刻の大小をテストの合否条件にはしません。
void RunSceneViewMeasurements();
}
