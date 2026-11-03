#include "Raven/Data/DataReader.h"
#include "Raven/Data/DataWriter.h"

#include <new>
#include <stdexcept>
#include <vector>

namespace Raven
{
namespace Data
{
DataReader::DataReader(DataElement root) : m_Root(root)
{
}

DataElement DataReader::GetRoot() const
{
    return m_Root;
}

bool DataReader::Read(DataWriter& writer, DataError* error) const
{
    if (error != nullptr) { *error = {}; }
    auto fail = [&](DataErrorCode code, std::uint32_t offset = 0)
    {
        if (error != nullptr) { *error = {code, offset}; }
        return false;
    };
    if (m_Root.IsValid() == false) { return fail(DataErrorCode::InvalidStructure); }
    try
    {
        struct Frame
        {
            DataElement Container;
            DataElement Next;
        };
        std::vector<Frame> stack;
        DataElement current = m_Root;
        std::string decoded;
        std::string_view text;
        auto readString = [&](DataElement element)
        {
            // 未Escape文字列はDocumentのViewをそのまま渡し、不要なコピー・確保を避けます。
            if (element.TryGetStringView(text) == true) { return true; }
            if (element.TryGetDecodedString(decoded) == false) { return false; }
            text = decoded;
            return true;
        };
        auto sinkFailed = [&](DataElement element)
        {
            auto code = writer.GetError().Code;
            if (code == DataErrorCode::None) { code = DataErrorCode::OutputFailure; }
            return fail(code, element.GetToken()->Offset);
        };
        while (true)
        {
            const auto* token = current.GetToken();
            bool accepted = false;
            switch (token->Type)
            {
            case DataType::Null: accepted = writer.WriteNull(); break;
            case DataType::Bool:
            {
                bool value = false;
                current.TryGetBool(value);
                accepted = writer.WriteBool(value);
                break;
            }
            case DataType::Int64:
            {
                std::int64_t value = 0;
                current.TryGetInt64(value);
                accepted = writer.WriteInt64(value);
                break;
            }
            case DataType::UInt64:
            {
                std::uint64_t value = 0;
                current.TryGetUInt64(value);
                accepted = writer.WriteUInt64(value);
                break;
            }
            case DataType::Float64:
            {
                double value = 0;
                current.TryGetFloat64(value);
                accepted = writer.WriteFloat64(value);
                break;
            }
            case DataType::String:
                if (readString(current) == false)
                {
                    // 検証済みDocumentのStringなので、展開失敗は呼び出し側Bufferの確保失敗です。
                    return fail(DataErrorCode::OutOfMemory, token->Offset);
                }
                accepted = writer.WriteString(text);
                break;
            case DataType::Binary:
            {
                std::string_view value;
                current.TryGetSource(value);
                accepted = writer.WriteBinary(value);
                break;
            }
            case DataType::Array: accepted = writer.BeginArray(); break;
            case DataType::Object: accepted = writer.BeginObject(); break;
            }
            if (accepted == false) { return sinkFailed(current); }
            if (token->Type == DataType::Array || token->Type == DataType::Object)
            {
                stack.push_back({current, current.GetFirstChild()});
            }

            // 子の部分木をSkipする兄弟Handleを保存し、index検索のO(n²)を避けます。
            while (stack.empty() == false)
            {
                auto& frame = stack.back();
                if (frame.Next.IsValid() == true)
                {
                    current = frame.Next;
                    if (frame.Container.IsType(DataType::Object) == true)
                    {
                        if (readString(current) == false)
                        {
                            return fail(DataErrorCode::OutOfMemory, current.GetToken()->Offset);
                        }
                        if (writer.WriteKey(text) == false) { return sinkFailed(current); }
                        current = current.GetNextSibling();
                    }
                    frame.Next = current.GetNextSibling();
                    break;
                }
                const bool closed = frame.Container.IsType(DataType::Array)
                    ? writer.EndArray() : writer.EndObject();
                if (closed == false) { return sinkFailed(frame.Container); }
                stack.pop_back();
            }
            if (stack.empty() == true) { return true; }
        }
    }
    catch (const std::bad_alloc&) { return fail(DataErrorCode::OutOfMemory); }
    catch (const std::length_error&) { return fail(DataErrorCode::LimitExceeded); }
}
}
}
