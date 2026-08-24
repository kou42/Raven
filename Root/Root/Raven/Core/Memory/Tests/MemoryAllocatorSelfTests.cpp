#include "Raven/Core/Memory/Tests/MemoryAllocatorSelfTests.h"

#include "Raven/Core/Memory/FrameAllocator.h"
#include "Raven/Core/Memory/LinearAllocator.h"
#include "Raven/Core/Memory/STLAllocatorAdapter.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace Raven::tests
{

namespace
{

bool IsAligned(const void* memory, std::size_t alignment)
{
    if (memory == nullptr || alignment == 0)
    {
        return false;
    }

    const std::uintptr_t address = reinterpret_cast<std::uintptr_t>(memory);
    return (address % alignment) == 0;
}

} // namespace

void RunMemoryAllocatorSelfTests()
{
    // ========================================================================
    // 1. LinearAllocator: 基本確保 / alignment / Reset
    // ========================================================================
    alignas(64) std::byte linearBuffer[256]{};
    LinearAllocator linearAllocator(linearBuffer, sizeof(linearBuffer));

    void* first = linearAllocator.Allocate(13, 8);
    void* second = linearAllocator.Allocate(17, 32);

    assert(first != nullptr);
    assert(second != nullptr);
    assert(IsAligned(first, 8));
    assert(IsAligned(second, 32));
    assert(linearAllocator.GetAllocationCount() == 2);
    assert(linearAllocator.GetUsedMemory() > 0);

    const std::size_t peakBeforeReset = linearAllocator.GetPeakUsedMemory();
    assert(peakBeforeReset == linearAllocator.GetUsedMemory());

    linearAllocator.Reset();
    assert(linearAllocator.GetUsedMemory() == 0);
    assert(linearAllocator.GetAllocationCount() == 0);

    // High Water MarkはReset後も維持し、必要容量の診断に使用します。
    assert(linearAllocator.GetPeakUsedMemory() == peakBeforeReset);

    // ========================================================================
    // 2. 不正入力 / 容量超過
    // ========================================================================
    assert(linearAllocator.Allocate(0, 8) == nullptr);
    assert(linearAllocator.Allocate(8, 3) == nullptr);
    assert(linearAllocator.Allocate(sizeof(linearBuffer) + 1, 1) == nullptr);

    // ========================================================================
    // 3. FrameAllocator: フレーム境界で一括再利用
    // ========================================================================
    FrameAllocator frameAllocator(512);

    void* frameMemory = frameAllocator.Allocate(64, 16);
    assert(frameMemory != nullptr);
    assert(IsAligned(frameMemory, 16));
    assert(frameAllocator.GetUsedMemory() >= 64);
    assert(frameAllocator.GetAllocationCount() == 1);

    const std::size_t framePeak = frameAllocator.GetPeakUsedMemory();
    frameAllocator.ResetFrame();

    assert(frameAllocator.GetUsedMemory() == 0);
    assert(frameAllocator.GetAllocationCount() == 0);
    assert(frameAllocator.GetPeakUsedMemory() == framePeak);

    // ========================================================================
    // 4. STLAllocatorAdapter: std::vectorの確保元をFrameAllocatorへ差し替える
    // ========================================================================
    // vector自身の寿命よりframeAllocatorの寿命が長いことが前提です。
    // vector破棄時のdeallocate()はFrameAllocatorではno-opであり、最終的な再利用は
    // ResetFrame()が担当します。
    using IntAllocator = STLAllocatorAdapter<int>;
    std::vector<int, IntAllocator> values{ IntAllocator(frameAllocator) };

    values.reserve(32);
    for (int value = 0; value < 32; ++value)
    {
        values.push_back(value * 2);
    }

    assert(values.size() == 32);
    assert(values.front() == 0);
    assert(values.back() == 62);
    assert(frameAllocator.GetAllocationCount() > 0);
    assert(frameAllocator.GetUsedMemory() > 0);

    // vectorが生存している間にResetFrame()してはいけません。
    // 実運用では一時Containerのscope終了後、フレーム境界でResetFrame()を呼びます。
}

} // namespace Raven::tests
