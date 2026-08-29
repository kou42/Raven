#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Raven
{
namespace Core
{

// ============================================================================
// JsonValue
// ============================================================================
// Engine全体でJSON Assetを外部ライブラリへ依存せず扱うためのJSON値型です。
//
// JSON Object / Array / String / Number / Boolean / Nullを全て保持します。
// Parser / WriterをAsset固有の意味解釈から分離し、glTFやProfileなど複数形式から
// 同じ構文処理を再利用できるようにします。
class JsonValue
{
public:
    enum class Type
    {
        Null,
        Boolean,
        Number,
        String,
        Array,
        Object
    };

    using Array = std::vector<JsonValue>;
    using Object = std::unordered_map<std::string, JsonValue>;

    JsonValue() = default;
    explicit JsonValue(bool value)
        : m_Type(Type::Boolean), m_Boolean(value)
    {
    }
    explicit JsonValue(double value)
        : m_Type(Type::Number), m_Number(value)
    {
    }
    explicit JsonValue(std::string value)
        : m_Type(Type::String), m_String(std::move(value))
    {
    }
    explicit JsonValue(Array value)
        : m_Type(Type::Array), m_Array(std::move(value))
    {
    }
    explicit JsonValue(Object value)
        : m_Type(Type::Object), m_Object(std::move(value))
    {
    }

    Type GetType() const { return m_Type; }

    bool IsNull() const { return m_Type == Type::Null; }
    bool IsBoolean() const { return m_Type == Type::Boolean; }
    bool IsNumber() const { return m_Type == Type::Number; }
    bool IsString() const { return m_Type == Type::String; }
    bool IsArray() const { return m_Type == Type::Array; }
    bool IsObject() const { return m_Type == Type::Object; }

    bool GetBoolean(bool defaultValue = false) const
    {
        return IsBoolean() ? m_Boolean : defaultValue;
    }

    double GetNumber(double defaultValue = 0.0) const
    {
        return IsNumber() ? m_Number : defaultValue;
    }

    const std::string& GetString() const { return m_String; }
    const Array& GetArray() const { return m_Array; }
    const Object& GetObject() const { return m_Object; }

    // Object参照時にoperator[]を使わないのは、読み取り処理で存在しないKeyを
    // 誤って追加してしまうことを防ぐためです。
    const JsonValue* Find(const std::string& key) const
    {
        if (IsObject() == false)
        {
            return nullptr;
        }

        const auto it = m_Object.find(key);
        if (it == m_Object.end())
        {
            return nullptr;
        }

        return &it->second;
    }

private:
    Type m_Type = Type::Null;
    bool m_Boolean = false;
    double m_Number = 0.0;
    std::string m_String;
    Array m_Array;
    Object m_Object;
};

} // namespace Core
} // namespace Raven
