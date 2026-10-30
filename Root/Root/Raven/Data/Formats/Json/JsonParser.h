#pragma once

#include "Raven/Data/DataDocument.h"

namespace Raven
{
namespace Data
{
// Coreと字句規則を共有し、JsonValueを経由せずSource-backed Tapeを作ります。
class JsonParser
{
public:
    // 成功時だけoutputを置換します。error.Offsetは0始まりの入力Byte位置です。
    static bool Parse(std::string_view text, DataDocument& output,
        DataError* error = nullptr, const DataLimits& limits = {});
};
}
}
