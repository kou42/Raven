#include "Raven/Diagnostics/DiagnosticValidation.h"
#include "Raven/Diagnostics/DiagnosticAPI.h"
#include "Raven/Diagnostics/DiagnosticMeasurement.h"

#include <algorithm>
#include <atomic>
#include <cstdio>

namespace Raven::Diagnostics
{
namespace
{
std::atomic<std::uint64_t> s_Fallbacks{ 0 };
thread_local bool s_Reporting = false;

std::string CopyText(std::string_view text, std::size_t limit)
{
    std::size_t size = std::min(text.size(), limit);
    while (size < text.size() && size > 0 && (static_cast<unsigned char>(text[size]) & 0xc0u) == 0x80u)
    {
        --size;
    }
    return std::string(text.substr(0, size));
}

void Fallback(DiagnosticValidationBackend backend, DiagnosticSeverity severity,
    std::int64_t id, std::string_view text) noexcept
{
    s_Fallbacks.fetch_add(1, std::memory_order_relaxed);
    // DiagnosticsのmutexやSinkを通さず、停止後・再入・Queue満杯でも元エラーを残します。
    // stdioのbest-effortであり、signal-safeやprocess破損時の保存保証はありません。
    const auto size = static_cast<int>(std::min(text.size(), MaxValidationMessageBytes));
    const char* level = severity == DiagnosticSeverity::Error ? "ERROR"
        : (severity == DiagnosticSeverity::Warning ? "WARNING" : "INFO");
    std::fprintf(stderr, "[%s][%s][ID %lld] %.*s\n",
        backend == DiagnosticValidationBackend::Vulkan ? "Vulkan Validation" : "DX12 InfoQueue",
        level, static_cast<long long>(id), size,
        text.empty() == false ? text.data() : "");
    std::fflush(stderr);
}

void Report(DiagnosticValidationBackend backend, DiagnosticSeverity severity, std::uint32_t nativeSeverity,
    std::uint32_t type, std::int32_t id, std::string_view name, std::string_view message) noexcept
{
    if (s_Reporting == true || IsDiagnosticCategoryEnabled(DiagnosticCategory::RHI) == false)
    {
        Fallback(backend, severity, id, message);
        return;
    }
    s_Reporting = true;
    try
    {
        DiagnosticValidation validation;
        validation.Backend = backend;
        validation.MessageId = id;
        validation.NativeSeverity = nativeSeverity;
        validation.NativeType = type;
        validation.FrameId = GetDiagnosticFrameId();
        validation.Name = CopyText(name, MaxValidationNameBytes);
        validation.Message = CopyText(message, MaxValidationMessageBytes);
        validation.NameTruncated = validation.Name.size() < name.size();
        validation.MessageTruncated = validation.Message.size() < message.size();
        if (TryEmitValidation(severity, std::move(validation)) == false)
        {
            Fallback(backend, severity, id, message);
        }
    }
    catch (...)
    {
        // Native ABIへ例外を伝えません。確保失敗時も借用中の元本文を即時出力できます。
        Fallback(backend, severity, id, message);
    }
    s_Reporting = false;
}
}

bool DiagnosticValidation::IsValid() const noexcept
{
    return (Backend == DiagnosticValidationBackend::Vulkan || Backend == DiagnosticValidationBackend::DirectX12)
        && Name.size() <= MaxValidationNameBytes && Message.size() <= MaxValidationMessageBytes;
}

std::string_view BoundedValidationString(const char* text, std::size_t maxBytes) noexcept
{
    if (text == nullptr)
    {
        return {};
    }
    std::size_t size = 0;
    while (size < maxBytes && text[size] != '\0') { ++size; }
    return std::string_view(text, size);
}

void ReportVulkanValidation(std::uint32_t severity, std::uint32_t type, std::int32_t id,
    std::string_view name, std::string_view message) noexcept
{
    // VkDebugUtilsMessageSeverityFlagBitsEXTの値。SDK側の接続点でもstatic_assertします。
    const auto common = (severity & 0x1000u) != 0 ? DiagnosticSeverity::Error
        : ((severity & 0x100u) != 0 ? DiagnosticSeverity::Warning : DiagnosticSeverity::Info);
    Report(DiagnosticValidationBackend::Vulkan, common, severity, type, id, name, message);
}

void ReportDX12Validation(std::uint32_t severity, std::int32_t id, std::string_view message) noexcept
{
    // D3D12_MESSAGE_SEVERITY: CORRUPTION=0 / ERROR=1 / WARNING=2 / INFO=3 / MESSAGE=4。
    const auto common = severity <= 1 ? DiagnosticSeverity::Error
        : (severity == 2 ? DiagnosticSeverity::Warning : DiagnosticSeverity::Info);
    Report(DiagnosticValidationBackend::DirectX12, common, severity, 0, id, {}, message);
}

std::uint64_t GetValidationFallbackCount() noexcept
{
    return s_Fallbacks.load(std::memory_order_relaxed);
}
}
