// Raven/Gltf/JsonParser.h
#pragma once

#include <string>
#include <string_view>

#include "Raven/Core/JsonValue.h"

namespace Raven
{
namespace Core
{

// ============================================================================
// JsonParser
// ============================================================================
// Engine内のJSON文字列を外部JSONライブラリへ依存せず解析するParserです。
//
// この層はJSON構文だけを担当し、glTFやProfile固有の意味は一切解釈しません。
class JsonParser
{
public:
    // 成功時はoutValueへRoot JSON Valueを格納します。
    // 失敗時はfalseを返し、errorMessageが指定されていれば原因とByte位置を格納します。
    static bool Parse(
        std::string_view text,
        JsonValue& outValue,
        std::string* errorMessage = nullptr);
};

} // namespace Core
} // namespace Raven
