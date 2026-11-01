#pragma once

#include "Raven/Data/DataElement.h"
#include "Raven/Data/DataWriter.h"
#include <array>
#include <type_traits>
#include <vector>

namespace Raven
{
namespace Data
{
enum class SerializationErrorCode
{
    None, MissingField, TypeMismatch, NumericRange, UnsupportedVersion,
    ValidationFailed, OutOfMemory, LimitExceeded, OutputFailure
};

// JSON Pointer形式のDecodedフィールド経路。報告自体は確保せず、長すぎる経路は拒否します。
struct SerializationError
{
    SerializationErrorCode Code = SerializationErrorCode::None;
    DataError Cause;
    std::array<char, 512> Path{};
};

// 呼び出し中だけ有効。子Contextも同じ最初のエラーを共有します。
class SerializationContext
{
public:
    explicit SerializationContext(SerializationError& error);
    SerializationContext Field(std::string_view name) const;
    SerializationContext Index(std::size_t index) const;
    bool Good() const;
    bool Fail(SerializationErrorCode code, DataElement element = {}, DataError cause = {}) const;
    bool Written(bool success, const DataWriter& writer) const;
private:
    SerializationError* m_Error;
    std::array<char, 512> m_Path{};
    std::size_t m_Length = 0;
};

// 生Pointerには既定Adapterを与えません。利用側は安定IDを明示的に保存し、参照解決を別途行います。
// 特殊化のReadは一時値だけを変更し、検証完了後にtrueを返してください。
template<class T, class Enable = void> struct DataAdapter;

template<class T>
struct DataAdapter<T, std::enable_if_t<std::is_arithmetic<T>::value>>
{
    static bool Read(DataElement input, T& output, SerializationContext& context);
    static bool Write(DataWriter& writer, const T& value, SerializationContext& context);
};
template<> struct DataAdapter<std::string>
{
    static bool Read(DataElement input, std::string& output, SerializationContext& context);
    static bool Write(DataWriter& writer, const std::string& value, SerializationContext& context);
};
template<class T> struct DataAdapter<std::vector<T>>
{
    static bool Read(DataElement input, std::vector<T>& output, SerializationContext& context);
    static bool Write(DataWriter& writer, const std::vector<T>& value, SerializationContext& context);
};

class ObjectReader
{
public:
    ObjectReader(DataElement input, SerializationContext& context);
    template<class T> bool Required(std::string_view name, T& output);
    // 欠落だけに既定値を適用します。明示Null・型不一致は拒否します。
    template<class T> bool Optional(std::string_view name, T& output, const T& defaultValue = T{});
    // type / versionは必須。整数TokenだけをVersionとして受理します。
    bool Header(std::string_view type, std::uint64_t oldest, std::uint64_t current, std::uint64_t& version);
private:
    bool Find(std::string_view name, DataElement& output, bool& found);
    DataElement m_Input;
    SerializationContext& m_Context;
};

template<class T>
bool WriteField(DataWriter& writer, std::string_view name, const T& value, SerializationContext& context);
// Adapterの部分変更を外へ公開せず、noexcept swapで全成功後だけCommitします。
template<class T>
bool Deserialize(DataElement input, T& output, SerializationError* error = nullptr);
template<class T>
bool Serialize(DataWriter& writer, const T& value, SerializationError* error = nullptr);
}
}
#include "Raven/Data/Serialization/Serialization.inl"
