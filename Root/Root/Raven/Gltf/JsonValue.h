// Raven/Gltf/JsonValue.h
#pragma once

#include "Raven/Core/JsonValue.h"

namespace Raven
{
namespace Gltf
{

// 既存glTF APIの型名を維持する互換Aliasです。実体と責務はCore層にあります。
using JsonValue = Core::JsonValue;

} // namespace Gltf
} // namespace Raven
