#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace Raven::Diagnostics
{
enum class DiagnosticValidationBackend : std::uint8_t { Vulkan = 1, DirectX12 = 2 };
constexpr std::size_t MaxValidationNameBytes = 128;
constexpr std::size_t MaxValidationMessageBytes = 1024;

// Native callbackの参照は保持せず、遅延出力まで本文と元IDを所有します。
struct DiagnosticValidation
{
    DiagnosticValidationBackend Backend = DiagnosticValidationBackend::Vulkan;
    std::int64_t MessageId = 0;
    std::uint32_t NativeSeverity = 0;
    std::uint32_t NativeType = 0;
    std::uint64_t FrameId = 0;
    std::string Name;
    std::string Message;
    bool NameTruncated = false;
    bool MessageTruncated = false;
    bool IsValid() const noexcept;
    bool operator==(const DiagnosticValidation&) const = default;
};

// SDKに依存しない変換入口。元SeverityはPayloadへ残し、共通Severityへ変換します。
void ReportVulkanValidation(std::uint32_t severity, std::uint32_t type, std::int32_t id,
    std::string_view name, std::string_view message) noexcept;
void ReportDX12Validation(std::uint32_t severity, std::int32_t id, std::string_view message) noexcept;
std::string_view BoundedValidationString(const char* text, std::size_t maxBytes) noexcept;
std::uint64_t GetValidationFallbackCount() noexcept;
}
