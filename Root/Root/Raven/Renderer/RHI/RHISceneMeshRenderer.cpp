#include "Raven/Renderer/RHI/RHISceneMeshRenderer.h"
#include "Raven/Diagnostics/DiagnosticMeasurement.h"

namespace Raven
{
RHIFrameResult RHISceneMeshRenderer::FinishActiveFrame(RHISceneFrameLifecycle& frame)
{
    RHIFrameResult end;
    {
        // GPU完了時間ではなく、CPU側のEndFrame/Submit処理時間だけを計測します。
        // Presentを区間に含めず、失敗時の既存の呼出順・戻り値も維持します。
        Diagnostics::ProfiledTraceScope trace("RHI.Submit", Diagnostics::DiagnosticCategory::RHI);
        end = frame.EndFrame();
    }
    Diagnostics::RecordCounter("RHI.Submit.Success", end == RHIFrameResult::Success ? 1.0 : 0.0,
        Diagnostics::DiagnosticCategory::RHI);
    if (end != RHIFrameResult::Success)
    {
        return end;
    }
    return frame.Present();
}
}
