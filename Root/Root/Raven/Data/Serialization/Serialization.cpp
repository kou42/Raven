#include "Raven/Data/Serialization/Serialization.h"
#include <charconv>

namespace Raven
{
namespace Data
{
SerializationContext::SerializationContext(SerializationError& error) : m_Error(&error) {}
SerializationContext SerializationContext::Field(std::string_view name) const
{
    auto child = *this;
    if (Good() == false) { return child; }
    auto append = [&](char byte)
    {
        if (child.m_Length + 1 >= child.m_Path.size())
        {
            child.Fail(SerializationErrorCode::LimitExceeded);
            return false;
        }
        child.m_Path[child.m_Length++] = byte;
        child.m_Path[child.m_Length] = '\0';
        return true;
    };
    if (append('/') == false) { return child; }
    for (const char byte : name)
    {
        // NULはC文字列診断を切断するため、対応外経路として明示的に拒否します。
        if (byte == '\0') { child.Fail(SerializationErrorCode::LimitExceeded); return child; }
        if (byte == '~' || byte == '/')
        {
            if (append('~') == false || append(byte == '~' ? '0' : '1') == false) { return child; }
        }
        else if (append(byte) == false) { return child; }
    }
    return child;
}
SerializationContext SerializationContext::Index(std::size_t index) const
{
    char buffer[32];
    const auto result = std::to_chars(buffer, buffer + sizeof(buffer), index);
    return Field(std::string_view(buffer, static_cast<std::size_t>(result.ptr - buffer)));
}
bool SerializationContext::Good() const { return m_Error->Code == SerializationErrorCode::None; }
bool SerializationContext::Fail(SerializationErrorCode code, DataElement element, DataError cause) const
{
    if (Good())
    {
        m_Error->Code = code;
        m_Error->Path = m_Path;
        if (element.GetToken() != nullptr) { cause.Offset = element.GetToken()->Offset; }
        m_Error->Cause = cause;
    }
    return false;
}
bool SerializationContext::Written(bool success, const DataWriter& writer) const
{
    if (Good() == false) { return false; }
    return success ? true : Fail(SerializationErrorCode::OutputFailure, {}, writer.GetError());
}
bool DataAdapter<std::string>::Read(DataElement input, std::string& output, SerializationContext& context)
{
    if (context.Good() == false) { return false; }
    if (input.IsType(DataType::String) == false) { return context.Fail(SerializationErrorCode::TypeMismatch, input); }
    if (input.TryGetDecodedString(output)) { return true; }
    return context.Fail(SerializationErrorCode::OutOfMemory, input);
}
bool DataAdapter<std::string>::Write(DataWriter& writer, const std::string& value, SerializationContext& context)
{
    return context.Good() && context.Written(writer.WriteString(value), writer);
}
ObjectReader::ObjectReader(DataElement input, SerializationContext& context) : m_Input(input), m_Context(context)
{
    if (input.IsType(DataType::Object) == false) { context.Fail(SerializationErrorCode::TypeMismatch, input); }
}
bool ObjectReader::Find(std::string_view name, DataElement& output, bool& found)
{
    auto nested = m_Context.Field(name);
    if (nested.Good() == false) { return false; }
    // TryFindのfalseは欠落と確保失敗を区別できません。ここではDecodedキーを明示走査し、
    // 展開失敗を既定値で隠さないようにします。未知フィールドはDocumentで検証済みです。
    for (auto key = m_Input.GetFirstChild(); key.IsValid();)
    {
        std::string decoded;
        std::string_view text;
        if (key.TryGetStringView(text) == false)
        {
            if (key.TryGetDecodedString(decoded) == false)
            { return nested.Fail(SerializationErrorCode::OutOfMemory, key); }
            text = decoded;
        }
        const auto value = key.GetNextSibling();
        if (text == name) { output = value; found = true; return true; }
        key = value.GetNextSibling();
    }
    found = false;
    return true;
}
bool ObjectReader::Header(std::string_view type, std::uint64_t oldest, std::uint64_t current, std::uint64_t& version)
{
    std::string actual;
    if (Required("type", actual) == false) { return false; }
    auto typeContext = m_Context.Field("type");
    if (actual != type) { return typeContext.Fail(SerializationErrorCode::ValidationFailed, m_Input); }
    DataElement field;
    bool found = false;
    auto versionContext = m_Context.Field("version");
    if (Find("version", field, found) == false) { return false; }
    if (found == false) { return versionContext.Fail(SerializationErrorCode::MissingField, m_Input); }
    if (field.IsType(DataType::Int64) == false && field.IsType(DataType::UInt64) == false)
    { return versionContext.Fail(SerializationErrorCode::TypeMismatch, field); }
    std::uint64_t value = 0;
    if (oldest == 0 || oldest > current || field.TryGetUInt64(value) == false || value < oldest || value > current)
    { return versionContext.Fail(SerializationErrorCode::UnsupportedVersion, field); }
    version = value;
    return true;
}
}
}
