#pragma once

namespace Raven
{
// --scene-vulkan専用。通常Mesh / Material / Camera / Renderer Queueを
// VulkanSceneRuntimeへ流し、Legacy OpenGL Resourceを生成せずに描画します。
int RunVulkanSceneRuntimeDemo();
} // namespace Raven
