// Raven/Gltf/JsonParser.h
#pragma once

#include <string>
#include <string_view>

#include "Raven/Gltf/JsonValue.h"

namespace Raven
{
namespace Gltf
{

// ============================================================================
// JsonParser
// ============================================================================
// glTF JSON Chunkを外部JSONライブラリへ依存せず解析する最小Parserです。
//
// この層はJSON構文だけを担当し、buffer / accessor / meshなどglTF固有の意味は
// 一切解釈しません。JSON構文解析とglTF Document構築を分離することで、
// 将来Mesh/Skin/Animationの対応範囲が増えてもParserを変更せずに済むようにします。
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

} // namespace Gltf
} // namespace Raven
