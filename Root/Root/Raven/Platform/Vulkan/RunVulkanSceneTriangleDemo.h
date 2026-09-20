#pragma once

namespace Raven
{
// --scene-triangle-vulkan専用。通常のOpenGL Applicationには影響しません。
// SPIR-Vのコンパイルは事前にVulkan SDKのglslcで行ってください。
int RunVulkanSceneTriangleDemo();
} // namespace Raven
