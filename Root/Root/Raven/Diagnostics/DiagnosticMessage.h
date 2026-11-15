#pragma once
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace Raven::Diagnostics
{
enum class DiagnosticArgumentType : std::uint8_t { Integer = 1, Real, Boolean, Text };
using DiagnosticArgument = std::variant<std::int64_t, double, bool, std::string>;
constexpr std::size_t MaxMessageArguments = 8;
constexpr std::size_t MaxMessageArgumentTextBytes = 256;
constexpr std::size_t MaxMessageArgumentBytes = 1024;

// ProducerからCollectorまで所有値だけを渡します。翻訳文やDictionary参照は保存しません。
struct DiagnosticMessage
{
    std::uint16_t DictionaryVersion = 1;
    std::uint16_t SchemaVersion = 0;
    std::uint64_t SchemaHash = 0;
    std::vector<DiagnosticArgument> Arguments;
    bool IsValid() const noexcept;
    bool operator==(const DiagnosticMessage&) const = default;
};
}
