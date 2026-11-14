#include "Raven/Renderer/RendererFrameDiagnostics.h"
#include "Raven/Renderer/Renderer.h"
#include "Raven/Diagnostics/DiagnosticMeasurement.h"

namespace Raven
{
RendererFrameDiagnostics::RendererFrameDiagnostics() noexcept
    : m_FrameId(Diagnostics::GetDiagnosticFrameId())
{
}
RendererFrameDiagnostics::~RendererFrameDiagnostics()
{
    // 入れ子のBeginFrameで別Frameへ移った場合、古いIDと新しい統計を混同しません。
    if (m_FrameId == Diagnostics::GetDiagnosticFrameId())
    {
        Renderer::PublishFrameDiagnostics();
    }
}
}
