#pragma once

#include "Raven/Data/DataElement.h"
#include <string>
#include <vector>

namespace Raven
{
namespace Core { class JsonValue; }
namespace Data
{
// 入力とTapeを所有する不変Document。要素ごとの所有Objectは生成しません。
// Move元・Move先双方の既存Element / Viewは無効です。Copyは許可しません。
// 読み取りは並列利用可。ただしMove・破棄と競合させないでください。
class DataDocument
{
public:
    DataDocument() = default;
    DataDocument(const DataDocument&) = delete;
    DataDocument& operator=(const DataDocument&) = delete;
    DataDocument(DataDocument&& other) noexcept;
    DataDocument& operator=(DataDocument&& other) noexcept;

    // 手動Tapeの入口。Source / Tokensはコピーして所有し、全検証後だけoutputへ反映します。
    // falseの場合はoutputを維持します（引数がoutput内部を参照していても同様）。
    static bool Create(std::string_view source, const std::vector<DataToken>& tokens,
        DataDocument& output, DataError* error = nullptr, const DataLimits& limits = {});
    // 既存Numberのdouble精度を維持する一方向Adapter。整数精度の復元は行いません。
    static bool FromJsonValue(const Core::JsonValue& value, DataDocument& output,
        DataError* error = nullptr, const DataLimits& limits = {});

    DataElement GetRoot() const;
    std::size_t GetTokenCount() const { return m_Tokens.size(); }
    std::string_view GetSource() const { return m_Source; }

private:
    friend class DataElement;
    std::string m_Source;
    std::vector<DataToken> m_Tokens;
};
}
}
