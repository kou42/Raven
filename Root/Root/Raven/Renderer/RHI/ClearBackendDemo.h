#pragma once

#include "Raven/Renderer/RHI/RHITypes.h"

namespace Raven
{
// 独立したNo-API WindowでClear/Present/Resizeを確認する起動経路。
// 既存OpenGL Application/Editorは起動せず、GPU ResourceをWindowより先に解放します。
int RunClearBackendDemo(RHIBackend backend);
} // namespace Raven
