#include "Raven/Diagnostics/DiagnosticSink.h"

namespace Raven::Diagnostics
{
bool DiagnosticFilter::Accepts(const DiagnosticEvent& event) const noexcept
{
    const auto category = static_cast<unsigned>(event.Category);
    const auto severity = static_cast<unsigned>(event.Severity);
    return category < DiagnosticCategoryCount && severity <= static_cast<unsigned>(DiagnosticSeverity::Error)
        && severity >= static_cast<unsigned>(MinimumSeverity)
        && (CategoryMask & (1u << category)) != 0u
        && (IncludeMeasurements == true || std::holds_alternative<DiagnosticMetric>(event.Payload) == false);
}

ConsoleSink::ConsoleSink(std::ostream& output, DiagnosticFilter filter)
    : m_Output(output), m_Filter(filter)
{
}

bool ConsoleSink::Write(const DiagnosticEvent& event)
{
    if (m_Filter.Accepts(event) == true)
    {
        WriteConsoleEvent(m_Output, event);
    }
    return m_Output.good();
}

bool ConsoleSink::Flush()
{
    m_Output.flush();
    return m_Output.good();
}

FileSink::FileSink(const std::filesystem::path& path, DiagnosticFilter filter)
    : m_Output(path, std::ios::out | std::ios::binary | std::ios::trunc), m_Filter(filter)
{
}

bool FileSink::IsOpen() const noexcept
{
    return m_Output.is_open() && m_Output.good();
}

bool FileSink::Write(const DiagnosticEvent& event)
{
    if (m_Filter.Accepts(event) == true)
    {
        WriteConsoleEvent(m_Output, event);
    }
    return IsOpen();
}

bool FileSink::Flush()
{
    m_Output.flush();
    return IsOpen();
}
}
