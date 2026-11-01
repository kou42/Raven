#pragma once

#include "Raven/Data/DataToken.h"
#include <string_view>

namespace Raven
{
namespace Data
{
struct DataWriteLimits
{
    std::uint32_t MaxOutputBytes = 64u * 1024u * 1024u;
    std::uint32_t MaxDepth = 128u;
    std::uint32_t MaxValueBytes = 16u * 1024u * 1024u;
    std::uint32_t MaxContainerElements = 262144u;
};

// 型付きAdapterとDocument走査で共有する、フォーマット非依存の構造化イベント契約。
// ObjectはKey→値、Arrayは値を送り、Begin / Endを対応させます。Rootは1値だけです。
// String / Keyは展開済みUTF-8、Binaryは任意Byte列。Viewは呼び出し中だけ有効で構いません。
// false以降はWriterを破棄してください。部分出力の公開・復旧方法は具象Writerの責務です。
// 実装は出力失敗を例外のまま公開せず、falseとGetErrorで報告してください。
class DataWriter
{
public:
    virtual ~DataWriter();
    virtual bool WriteNull() = 0;
    virtual bool WriteBool(bool value) = 0;
    virtual bool WriteInt64(std::int64_t value) = 0;
    virtual bool WriteUInt64(std::uint64_t value) = 0;
    virtual bool WriteFloat64(double value) = 0;
    virtual bool WriteString(std::string_view value) = 0;
    virtual bool WriteBinary(std::string_view value) = 0;
    virtual bool BeginArray() = 0;
    virtual bool EndArray() = 0;
    virtual bool BeginObject() = 0;
    virtual bool WriteKey(std::string_view key) = 0;
    virtual bool EndObject() = 0;
    virtual DataError GetError() const = 0;
};
}
}
