// Raven/Core/JsonWriter.h
#pragma once

#include "Raven/Core/JsonValue.h"

#include <string>

namespace Raven
{
namespace Core
{

// JsonValueを決定的なUTF-8 JSON文字列へ変換します。
// indentSizeが0ならCompact、それ以外ならAssetをレビューしやすい整形形式になります。
class JsonWriter
{
public:
    static bool Write(
        const JsonValue& value,
        std::string& outText,
        std::string* errorMessage = nullptr,
        int indentSize = 2);
};

} // namespace Core
} // namespace Raven
