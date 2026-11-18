#pragma once

#include "Raven/Renderer/RHI/RHICompute.h"

namespace Raven
{

// Legacy GraphicsとComputeが同じContext上で3D Textureを共有するためのBackend境界です。
std::uint32_t GetOpenGLComputeTextureID(const Ref<RHIComputeResource>& resource);

} // namespace Raven
