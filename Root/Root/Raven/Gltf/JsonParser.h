// Raven/Gltf/JsonParser.h
#pragma once

#include "Raven/Core/JsonParser.h"
#include "Raven/Gltf/JsonValue.h"

namespace Raven
{
namespace Gltf
{

// glTF利用側の変更範囲を抑える互換Aliasです。構文解析はCore層が担当します。
using JsonParser = Core::JsonParser;

} // namespace Gltf
} // namespace Raven
