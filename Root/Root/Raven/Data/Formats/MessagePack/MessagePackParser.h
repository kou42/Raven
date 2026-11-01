#pragma once

#include "Raven/Data/DataDocument.h"

namespace Raven
{
namespace Data
{
// 長さ付きバイト列を所有Documentへ変換します。成功時だけoutputを置換します。
// MapキーはUTF-8 Stringのみ。Extension / Timestamp、非有限Floatは拒否します。
// Error.Offsetは入力Byte位置（EOFは入力長）。入力がoutputのSourceを参照していても安全です。
class MessagePackParser
{
public:
    static bool Parse(std::string_view bytes, DataDocument& output,
        DataError* error = nullptr, const DataLimits& limits = {});
};
}
}
