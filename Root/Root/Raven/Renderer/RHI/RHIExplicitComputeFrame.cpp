#include "Raven/Renderer/RHI/RHIExplicitComputeFrame.h"

namespace Raven
{

RHIExplicitComputeFrame::RHIExplicitComputeFrame(RecordFunction record)
    : m_Record(std::move(record))
{
}

bool RHIExplicitComputeFrame::Record()
{
    return m_Record(*this);
}

void RHIExplicitComputeFrame::Complete(bool gpuCompleted) noexcept
{
    for (const auto& completion : m_Completions)
    {
        completion(gpuCompleted);
    }
    m_Completions.clear();
}

void RHIExplicitComputeFrame::Retain(Ref<void> resource)
{
    m_Resources.push_back(std::move(resource));
}

void RHIExplicitComputeFrame::OnCompletion(std::function<void(bool)> completion)
{
    m_Completions.push_back(std::move(completion));
}

void RHICompletedComputeTimestamp::Complete(bool success, double nanoseconds) noexcept
{
    m_Status = success ? RHIComputeReadbackStatus::Ready : RHIComputeReadbackStatus::Failed;
    m_Nanoseconds = nanoseconds;
}

RHIComputeReadbackStatus RHICompletedComputeTimestamp::Poll(double& nanoseconds)
{
    if (m_Status == RHIComputeReadbackStatus::Ready)
    {
        nanoseconds = m_Nanoseconds;
    }
    return m_Status;
}

} // namespace Raven
