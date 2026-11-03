#pragma once
#include <cmath>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>

namespace Raven
{
namespace Data
{
template<class T>
bool Deserialize(DataElement input, T& output, SerializationError* error)
{
    static_assert(std::is_nothrow_swappable<T>::value, "Commit requires noexcept swap");
    SerializationError local;
    SerializationError& result = error != nullptr ? *error : local;
    result = {};
    SerializationContext context(result);
    try
    {
        T candidate{};
        if (DataAdapter<T>::Read(input, candidate, context) == false || context.Good() == false)
        {
            return context.Fail(SerializationErrorCode::ValidationFailed, input);
        }
        using std::swap;
        swap(output, candidate);
        return true;
    }
    catch (const std::bad_alloc&) { return context.Fail(SerializationErrorCode::OutOfMemory, input); }
    catch (const std::length_error&) { return context.Fail(SerializationErrorCode::LimitExceeded, input); }
}

template<class T>
bool Serialize(DataWriter& writer, const T& value, SerializationError* error)
{
    SerializationError local;
    SerializationError& result = error != nullptr ? *error : local;
    result = {};
    SerializationContext context(result);
    try
    {
        if (DataAdapter<T>::Write(writer, value, context) == false || context.Good() == false)
        {
            return context.Fail(SerializationErrorCode::OutputFailure, {}, writer.GetError());
        }
        return true;
    }
    catch (const std::bad_alloc&) { return context.Fail(SerializationErrorCode::OutOfMemory); }
    catch (const std::length_error&) { return context.Fail(SerializationErrorCode::LimitExceeded); }
}

template<class T>
bool DataAdapter<T, std::enable_if_t<std::is_arithmetic<T>::value>>::Read(
    DataElement input, T& output, SerializationContext& context)
{
    if (context.Good() == false) { return false; }
    if constexpr (std::is_same<T, bool>::value)
    {
        if (input.TryGetBool(output)) { return true; }
        return context.Fail(SerializationErrorCode::TypeMismatch, input);
    }
    else
    {
        if (input.IsType(DataType::Int64) == false && input.IsType(DataType::UInt64) == false
            && input.IsType(DataType::Float64) == false)
        {
            return context.Fail(SerializationErrorCode::TypeMismatch, input);
        }
        if constexpr (std::is_integral<T>::value && std::is_signed<T>::value)
        {
            std::int64_t value = 0;
            if (input.TryGetInt64(value) && value >= (std::numeric_limits<T>::min)()
                && value <= (std::numeric_limits<T>::max)())
            { output = static_cast<T>(value); return true; }
        }
        else if constexpr (std::is_integral<T>::value)
        {
            std::uint64_t value = 0;
            if (input.TryGetUInt64(value) && value <= (std::numeric_limits<T>::max)())
            { output = static_cast<T>(value); return true; }
        }
        else
        {
            static_assert(sizeof(T) <= sizeof(double), "Only float / double supported");
            double value = 0;
            if (input.TryGetFloat64(value) && std::abs(value) <= (std::numeric_limits<T>::max)())
            {
                const T narrowed = static_cast<T>(value);
                // floatへの縮小も精度損失・Underflowを黙認しません。
                if (static_cast<double>(narrowed) == value) { output = narrowed; return true; }
            }
        }
        return context.Fail(SerializationErrorCode::NumericRange, input);
    }
}

template<class T>
bool DataAdapter<T, std::enable_if_t<std::is_arithmetic<T>::value>>::Write(
    DataWriter& writer, const T& value, SerializationContext& context)
{
    if (context.Good() == false) { return false; }
    if constexpr (std::is_same<T, bool>::value) { return context.Written(writer.WriteBool(value), writer); }
    else if constexpr (std::is_integral<T>::value && std::is_signed<T>::value)
    { return context.Written(writer.WriteInt64(value), writer); }
    else if constexpr (std::is_integral<T>::value) { return context.Written(writer.WriteUInt64(value), writer); }
    else
    {
        static_assert(sizeof(T) <= sizeof(double), "Only float / double supported");
        if (std::isfinite(value) == false) { return context.Fail(SerializationErrorCode::NumericRange); }
        return context.Written(writer.WriteFloat64(value), writer);
    }
}

template<class T>
bool DataAdapter<std::vector<T>>::Read(DataElement input, std::vector<T>& output, SerializationContext& context)
{
    if (context.Good() == false) { return false; }
    if (input.IsType(DataType::Array) == false) { return context.Fail(SerializationErrorCode::TypeMismatch, input); }
    try
    {
        std::vector<T> candidate;
        std::size_t index = 0;
        // index APIの繰り返しによるO(n²)走査と巨大Countの先行Reserveを避けます。
        for (auto child = input.GetFirstChild(); child.IsValid(); child = child.GetNextSibling(), ++index)
        {
            auto nested = context.Index(index);
            T value{};
            if (nested.Good() == false || DataAdapter<T>::Read(child, value, nested) == false)
            { return nested.Fail(SerializationErrorCode::ValidationFailed, child); }
            candidate.push_back(std::move(value));
        }
        output.swap(candidate);
        return true;
    }
    catch (const std::bad_alloc&) { return context.Fail(SerializationErrorCode::OutOfMemory, input); }
    catch (const std::length_error&) { return context.Fail(SerializationErrorCode::LimitExceeded, input); }
}

template<class T>
bool DataAdapter<std::vector<T>>::Write(DataWriter& writer, const std::vector<T>& value, SerializationContext& context)
{
    if (context.Good() == false || context.Written(writer.BeginArray(), writer) == false) { return false; }
    for (std::size_t i = 0; i < value.size(); ++i)
    {
        auto nested = context.Index(i);
        if (nested.Good() == false || DataAdapter<T>::Write(writer, value[i], nested) == false)
        { return nested.Fail(SerializationErrorCode::OutputFailure, {}, writer.GetError()); }
    }
    return context.Written(writer.EndArray(), writer);
}

template<class T> bool ObjectReader::Required(std::string_view name, T& output)
{
    auto nested = m_Context.Field(name);
    DataElement field;
    bool found = false;
    if (nested.Good() == false || Find(name, field, found) == false) { return false; }
    if (found == false) { return nested.Fail(SerializationErrorCode::MissingField, m_Input); }
    try
    {
        if (DataAdapter<T>::Read(field, output, nested) == false)
        { return nested.Fail(SerializationErrorCode::ValidationFailed, field); }
        return nested.Good();
    }
    catch (const std::bad_alloc&) { return nested.Fail(SerializationErrorCode::OutOfMemory, field); }
    catch (const std::length_error&) { return nested.Fail(SerializationErrorCode::LimitExceeded, field); }
}

template<class T> bool ObjectReader::Optional(std::string_view name, T& output, const T& defaultValue)
{
    auto nested = m_Context.Field(name);
    DataElement field;
    bool found = false;
    if (nested.Good() == false || Find(name, field, found) == false) { return false; }
    try
    {
        if (found == false) { output = defaultValue; return true; }
        if (DataAdapter<T>::Read(field, output, nested) == false)
        { return nested.Fail(SerializationErrorCode::ValidationFailed, field); }
        return nested.Good();
    }
    catch (const std::bad_alloc&) { return nested.Fail(SerializationErrorCode::OutOfMemory, field); }
    catch (const std::length_error&) { return nested.Fail(SerializationErrorCode::LimitExceeded, field); }
}

template<class T>
bool WriteField(DataWriter& writer, std::string_view name, const T& value, SerializationContext& context)
{
    auto nested = context.Field(name);
    try
    {
        if (nested.Good() == false || nested.Written(writer.WriteKey(name), writer) == false) { return false; }
        if (DataAdapter<T>::Write(writer, value, nested) == false)
        { return nested.Fail(SerializationErrorCode::OutputFailure, {}, writer.GetError()); }
        return nested.Good();
    }
    catch (const std::bad_alloc&) { return nested.Fail(SerializationErrorCode::OutOfMemory); }
    catch (const std::length_error&) { return nested.Fail(SerializationErrorCode::LimitExceeded); }
}
}
}
