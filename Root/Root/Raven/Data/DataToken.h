#pragma once

#include <cstdint>
#include <limits>

namespace Raven
{
namespace Data
{
using TokenId = std::uint32_t;
inline constexpr TokenId InvalidTokenId = (std::numeric_limits<TokenId>::max)();

// StringのPayloadに保持するEncoding。数値Payloadのbit幅とTokenレイアウトは変えません。
inline constexpr std::uint64_t DecodedUtf8String = 0;
inline constexpr std::uint64_t JsonEscapedString = 1;

enum class DataType : std::uint32_t
{
    Null, Bool, Int64, UInt64, Float64, String, Binary, Array, Object
};

// Preorder Tape。Objectの直下はStringキー、値の順に並びます。
// Nextは部分木の直後（Token数と等しい終端も可）、CountはArray要素数 / Object組数です。
// Source範囲はDocumentが所有するBuffer内。StringはPayloadにより展開済みUTF-8またはJSON Escape表記を保持します。
// PayloadはBoolの0/1、UInt64の値、Int64 / Float64の64 bit表現、StringのEncodingです。
// Token幅は必要な範囲と値の保持を優先し、16 byteには固定しません。
struct DataToken
{
    DataType Type = DataType::Null;
    std::uint32_t Offset = 0;
    std::uint32_t Length = 0;
    TokenId Next = 0;
    std::uint32_t Count = 0;
    std::uint64_t Payload = 0;
};

struct DataLimits
{
    std::uint32_t MaxInputBytes = 64u * 1024u * 1024u;
    std::uint32_t MaxTokens = 1048576u;
    std::uint32_t MaxDepth = 128u;
    std::uint32_t MaxValueBytes = 16u * 1024u * 1024u;
    std::uint32_t MaxContainerElements = 262144u;
};

enum class DataErrorCode
{
    None, InvalidStructure, InvalidEncoding, DuplicateKey, LimitExceeded,
    InvalidLimits, NumericRange, OutOfMemory, InvalidSyntax, UnexpectedEnd, TrailingData
};

// エラー報告は追加の文字列確保に依存しません。OffsetはBuffer内のByte位置です。
struct DataError
{
    DataErrorCode Code = DataErrorCode::None;
    std::uint32_t Offset = 0;
};
}
}
