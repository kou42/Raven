#pragma once

#include "Raven/Renderer/RHI/RHICompute.h"

namespace Raven
{

// ContextがFrame Slotへ保持する記録単位です。DescriptorやReadbackが
// Encoderの呼び出し元から解放されても、記録後はこのFrameが所有します。
class RHIExplicitComputeFrame final : public RHIComputeFrameWork
{
public:
    using RecordFunction = std::function<bool(RHIExplicitComputeFrame&)>;
    explicit RHIExplicitComputeFrame(RecordFunction record);
    bool Record() override;
    void Complete(bool gpuCompleted) noexcept override;
    void Retain(Ref<void> resource);
    void OnCompletion(std::function<void(bool)> completion);

private:
    RecordFunction m_Record;
    std::vector<Ref<void>> m_Resources;
    std::vector<std::function<void(bool)>> m_Completions;
};

} // namespace Raven
