#pragma once

#include "Raven/Diagnostics/DiagnosticAPI.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace Raven::Diagnostics
{
// Phase 02: Producer/Consumerを各1スレッドに限定する固定長SPSC Buffer。
// 所有者は両スレッドの終了後に破棄します。スレッド登録・Collectorは後続実装です。
// 容量は256イベント。満杯の場合は新しいイベントを破棄してDroppedを増やします。
class EventBuffer final
{
public:
    static constexpr std::size_t Capacity = 256;

    EventBuffer() = default;
    EventBuffer(const EventBuffer&) = delete;
    EventBuffer& operator=(const EventBuffer&) = delete;

    bool TryPush(DiagnosticEvent event);
    bool TryPop(DiagnosticEvent& event);
    std::uint64_t GetDroppedCount() const noexcept;

private:
    // 単調増加indexを使い、wrap時も容量差分で満杯を判定します。
    // ReadIndexはConsumerのみ更新、WriteIndexはProducerのみ更新します。
    std::array<DiagnosticEvent, Capacity> m_Events{};
    alignas(64) std::atomic<std::uint64_t> m_ReadIndex{ 0 };
    alignas(64) std::atomic<std::uint64_t> m_WriteIndex{ 0 };
    std::atomic<std::uint64_t> m_DroppedCount{ 0 };
};
}
