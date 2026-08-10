// Raven/Animation/Tests/SkeletalDeformationSelfTests.h
#pragma once

namespace Raven::tests
{

// RendererやOpenGL Contextを必要とせず、CPU Geometry上だけで
// Bind Pose / 1 Bone / 2 Bone Linear Blend Skinningを検証します。
void RunSkeletalDeformationSelfTests();

} // namespace Raven::tests
