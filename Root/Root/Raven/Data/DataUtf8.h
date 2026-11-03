#pragma once

#include <string_view>

namespace Raven
{
namespace Data
{
namespace Detail
{
// 展開済み文字列のUTF-8検査。JSON Escape規則を適用せずNULと制御文字を許容します。
bool IsUtf8(std::string_view text);
}
}
}
