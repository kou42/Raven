#pragma once

#include "Raven/Data/DataToken.h"
#include <string>
#include <string_view>

namespace Raven
{
namespace Data
{
class DataDocument;

// 非所有Handle。Documentの破棄・置換・Move後は再取得してください。
// IsValidは破棄済みDocumentへのアクセスを安全にするものではありません。
class DataElement
{
public:
    DataElement() = default;
    bool IsValid() const;
    bool IsType(DataType type) const;
    bool TryGetType(DataType& output) const;
    TokenId GetId() const { return m_Id; }
    const DataToken* GetToken() const;
    // Try APIは型不一致・範囲外・確保失敗時にfalseを返し、出力を変更しません。
    // BoolとNumberの暗黙変換、文字列からの数値変換は行いません。
    bool TryGetBool(bool& output) const;
    bool TryGetInt64(std::int64_t& output) const;
    bool TryGetUInt64(std::uint64_t& output) const;
    bool TryGetFloat64(double& output) const;

    bool TryGetRawString(std::string_view& output) const;
    // Escapeを含まない展開済みUTF-8だけを、Document参照のViewとして返します。
    bool TryGetStringView(std::string_view& output) const;
    // 呼び出し側が所有するStringへ展開します。成功時は以前のoutputのViewが無効です。
    // Document内に可変Cacheを作らず、独立した出力Bufferなら並列呼び出し可能です。
    bool TryGetDecodedString(std::string& output) const;

    // Arrayは値数、ObjectはKey / Value組数。スカラーではfalseです。
    bool TryGetCount(std::uint32_t& output) const;
    // Tapeを走査するためindexへの取得はO(index)。連続走査には下記の子・兄弟APIを使います。
    bool TryGetArrayElement(std::uint32_t index, DataElement& output) const;
    // keyとvalueの出力に同じ変数を指定した場合はfalseです。
    bool TryGetObjectMember(std::uint32_t index, DataElement& key, DataElement& value) const;
    // DecodedキーのByte列を比較します（NULも可）。見つからない場合も出力は維持します。
    // Escapeキー用の一時Bufferは呼び出し内だけで所有し、確保失敗もfalseで返します。
    bool TryFind(std::string_view key, DataElement& output) const;
    DataElement GetFirstChild() const;
    // 同じ親の中だけを返します。Objectではキーの次は値になります。
    DataElement GetNextSibling() const;
    // 入力表記のView。JSON Stringは引用符を除き、Escapeがあれば未展開で返します。
    // Payload == JsonEscapedStringを展開済み文字列として扱わないでください。
    bool TryGetSource(std::string_view& output) const;

private:
    friend class DataDocument;
    DataElement(const DataDocument* document, TokenId id, TokenId end);
    const DataDocument* m_Document = nullptr;
    TokenId m_Id = InvalidTokenId;
    TokenId m_End = 0;
};
}
}
