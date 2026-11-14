#include "Raven/Diagnostics/DiagnosticMetric.h"

#include <algorithm>
#include <cmath>

namespace Raven::Diagnostics
{
void DiagnosticMetric::SetName(std::string_view name) noexcept
{
    Name.fill(0);
    std::size_t size = std::min(name.size(), Name.size());
    // 正しいUTF-8入力を前提とし、上限にかかる文字の継続byteだけを取り除きます。
    while (size < name.size() && size > 0
        && (static_cast<unsigned char>(name[size]) & 0xc0u) == 0x80u)
    {
        --size;
    }
    std::copy_n(name.begin(), size, Name.begin());
    NameBytes = static_cast<std::uint16_t>(size);
}

std::string_view DiagnosticMetric::GetName() const noexcept
{
    return std::string_view(Name.data(), std::min<std::size_t>(NameBytes, Name.size()));
}

bool DiagnosticMetric::IsValid() const noexcept
{
    if (NameBytes == 0 || NameBytes > Name.size() || std::isfinite(Value) == false)
    {
        return false;
    }
    if (Kind == DiagnosticMetricKind::Counter)
    {
        return ScopeId == 0 && ParentScopeId == 0;
    }
    return (Kind == DiagnosticMetricKind::TraceBegin || Kind == DiagnosticMetricKind::TraceEnd)
        && ScopeId != 0 && ParentScopeId != ScopeId && Value == 0.0;
}
}
