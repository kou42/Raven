#pragma once

#include <cstddef>
#include <cstdint>

namespace Raven
{

struct AllocationMeasurement
{
    bool Available = false;
    std::uint64_t Count = 0u;
    std::uint64_t Bytes = 0u;
};

// 計測用ビルドだけがC++ newを置換します。通常ビルドではfalseを返し、
// 未計測と「計測済み0件」を区別します。malloc / Driver / 別Threadは対象外です。
bool IsAllocationMeasurementEnabled() noexcept;
void RecordMeasuredAllocation(std::size_t bytes) noexcept;

// 現在のThreadの確保先集計だけを切り替えます。入れ子は最内側へ1回だけ加算し、
// 例外でも元の集計先へ戻します。MeasurementはScopeより長く生存する必要があります。
class AllocationMeasurementScope final
{
public:
    explicit AllocationMeasurementScope(AllocationMeasurement& measurement) noexcept;
    ~AllocationMeasurementScope();
    AllocationMeasurementScope(const AllocationMeasurementScope&) = delete;
    AllocationMeasurementScope& operator=(const AllocationMeasurementScope&) = delete;

private:
    AllocationMeasurement* m_Previous = nullptr;
};

} // namespace Raven
