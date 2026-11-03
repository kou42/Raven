#pragma once

#include "Raven/Data/DataWriter.h"
#include <string>
#include <vector>

namespace Raven
{
namespace Data
{
class DataElement;
class DataDocument;

// DataWriterイベントをMessagePackへ出力します。String / Binaryを区別し、有限Float64を保持します。
// 要素数がBegin時に不明なため符号化済みの子を一時保持し、EndでPrefixを確定します。
// ObjectはDecodedキー順で決定的に出力。重複キーは拒否します。
// 単一Thread専用。失敗はsticky。Finish成功時だけoutputへCommitします。
class MessagePackWriter final : public DataWriter
{
public:
    explicit MessagePackWriter(const DataWriteLimits& limits = {});
    MessagePackWriter(const MessagePackWriter&) = delete;
    MessagePackWriter& operator=(const MessagePackWriter&) = delete;
    MessagePackWriter(MessagePackWriter&&) = delete;
    MessagePackWriter& operator=(MessagePackWriter&&) = delete;

    bool WriteNull() override;
    bool WriteBool(bool value) override;
    bool WriteInt64(std::int64_t value) override;
    bool WriteUInt64(std::uint64_t value) override;
    bool WriteFloat64(double value) override;
    bool WriteString(std::string_view value) override;
    bool WriteBinary(std::string_view value) override;
    bool BeginArray() override;
    bool EndArray() override;
    bool BeginObject() override;
    bool WriteKey(std::string_view key) override;
    bool EndObject() override;
    // Offsetは失敗前に受理した符号化Byte数。ソート後の物理位置ではありません。
    DataError GetError() const override;

    // Rootが1つ完成し、全コンテナが閉じた場合だけoutputを置換します。呼び出しは1回限りです。
    bool Finish(std::string& output);
    static bool Write(DataElement root, std::string& output,
        DataError* error = nullptr, const DataWriteLimits& limits = {});
    static bool Write(const DataDocument& document, std::string& output,
        DataError* error = nullptr, const DataWriteLimits& limits = {});

private:
    enum class Event { Scalar, String, Binary, Key, BeginArray, EndArray, BeginObject, EndObject };
    struct Member
    {
        std::string Key;
        std::string EncodedKey;
        std::string Value;
    };
    struct Frame
    {
        bool Object = false;
        bool HasKey = false;
        std::uint32_t Count = 0;
        std::string Key;
        std::string EncodedKey;
        std::string Values;
        std::vector<Member> Members;
    };
    bool Process(Event event, std::string_view value = {});
    bool Fail(DataErrorCode code);
    bool Charge(std::size_t bytes);
    bool CheckValue();
    bool EncodeString(std::string_view value, std::string& output);
    void AppendValue(std::string value);

    DataWriteLimits m_Limits;
    DataError m_Error;
    // 先にコンテナの最小Headerを課金し、Count確定時に差分を課金します。
    std::size_t m_OutputBytes = 0;
    bool m_HasRoot = false;
    bool m_Finished = false;
    std::string m_Root;
    std::vector<Frame> m_Stack;
};
}
}
