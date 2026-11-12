#pragma once
#include "Raven/Data/DataDocument.h"
#include <string>
#include <string_view>

namespace Raven
{
namespace Data
{
// 汎用Document用の独立したファイルEnvelope。Runtime AssetとはMagicを共有しません。
// 全整数はLittle Endianで個別符号化し、C++構造体の配置には依存しません。
// 現行版は所有BufferへParseするため、memory mapping / zero-copyは行いません。
class BinaryDocument
{
public:
    static constexpr std::uint32_t Version = 1;
    static bool Write(const DataDocument& document, std::string& output, DataError* error = nullptr);
    static bool Read(std::string_view bytes, DataDocument& output, DataError* error = nullptr,
        const DataLimits& limits = {});
};
}
}
