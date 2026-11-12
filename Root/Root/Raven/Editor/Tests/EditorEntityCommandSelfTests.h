#pragma once

namespace Raven::tests
{

void RunEditorEntityCommandSelfTests();
// Releaseでもassertに依存せず、Scene交換前通知と参照解除を検証します。
void RunEditorSceneLifetimeSelfTests();

} // namespace Raven::tests
