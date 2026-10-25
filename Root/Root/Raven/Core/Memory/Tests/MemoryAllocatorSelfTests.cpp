#include "Raven/Core/Memory/Tests/MemoryAllocatorSelfTests.h"

#include "Raven/Core/Memory/FrameAllocator.h"
#include "Raven/Core/Memory/FreeListAllocator.h"
#include "Raven/Core/Memory/HeapAllocator.h"
#include "Raven/Core/Memory/LinearAllocator.h"
#include "Raven/Core/Memory/MemoryTracker.h"
#include "Raven/Core/Memory/PoolAllocator.h"
#include "Raven/Core/Memory/STLAllocatorAdapter.h"
#include "Raven/Core/Containers/Vector.h"
#include "Raven/Core/Containers/SmallVector.h"
#include "Raven/Core/Containers/FixedVector.h"
#include "Raven/Core/Containers/Span.h"
#include "Raven/Core/Containers/Array.h"
#include "Raven/Core/Containers/RingBuffer.h"
#include "Raven/Core/Containers/BitSet.h"
#include "Raven/Core/Containers/HandlePool.h"
#include "Raven/Core/Containers/SparseSet.h"

#include <cassert>
#include <algorithm>
#include <iterator>
#include <type_traits>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>
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

// 非trivial要素の生存数とcopy/moveを観測します。
struct VectorLifecycleValue
{
    static inline int live = 0;
    static inline int copies = 0;
    static inline int moves = 0;

    int value = 0;
    explicit VectorLifecycleValue(int number = 0) : value(number) { ++live; }
    VectorLifecycleValue(const VectorLifecycleValue& other) : value(other.value)
    {
        ++live;
        ++copies;
    }
    VectorLifecycleValue(VectorLifecycleValue&& other) noexcept : value(other.value)
    {
        ++live;
        ++moves;
    }
    ~VectorLifecycleValue() { --live; }
};

// 明示的にallocation失敗させ、Vectorの要素と容量が変化しないことを検証します。
class FailingVectorAllocator final : public Allocator
{
public:
    explicit FailingVectorAllocator(Allocator& backing) : m_Backing(backing) {}

    void* Allocate(std::size_t size, std::size_t alignment) override
    {
        if (m_Reject)
        {
            return nullptr;
        }
        return m_Backing.Allocate(size, alignment);
    }
    void Deallocate(void* memory) override { m_Backing.Deallocate(memory); }
    void Reset() override { m_Backing.Reset(); }
    std::size_t GetCapacity() const override { return m_Backing.GetCapacity(); }
    std::size_t GetUsedMemory() const override { return m_Backing.GetUsedMemory(); }
    std::size_t GetPeakUsedMemory() const override { return m_Backing.GetPeakUsedMemory(); }
    std::size_t GetAllocationCount() const override { return m_Backing.GetAllocationCount(); }
    void SetReject(bool reject) { m_Reject = reject; }

private:
    Allocator& m_Backing;
    bool m_Reject = false;
};

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
    // 4. HeapAllocator: 個別解放、alignment、統計、異常入力
    // ========================================================================
    HeapAllocator heapAllocator;
    void* heapFirst = heapAllocator.Allocate(13, 8);
    void* heapSecond = heapAllocator.Allocate(64, 64);
    assert(heapFirst != nullptr);
    assert(heapSecond != nullptr);
    assert(IsAligned(heapFirst, 8));
    assert(IsAligned(heapSecond, 64));
    assert(heapAllocator.GetUsedMemory() == 77);
    assert(heapAllocator.GetAllocationCount() == 2);
    assert(heapAllocator.GetPeakUsedMemory() == 77);

    heapAllocator.Deallocate(heapFirst);
    assert(heapAllocator.GetUsedMemory() == 64);
    assert(heapAllocator.GetAllocationCount() == 1);
    heapAllocator.Deallocate(nullptr);
    assert(heapAllocator.Allocate(0, 8) == nullptr);
    assert(heapAllocator.Allocate(8, 3) == nullptr);
    assert(heapAllocator.Allocate(8, 0) == nullptr);
    assert(heapAllocator.Allocate((std::numeric_limits<std::size_t>::max)(), 16) == nullptr);
    assert(heapAllocator.GetAllocationCount() == 1);
    heapAllocator.Reset();
    assert(heapAllocator.GetUsedMemory() == 0);
    assert(heapAllocator.GetAllocationCount() == 0);
    assert(heapAllocator.GetPeakUsedMemory() == 77);

    // ========================================================================
    // 5. FreeListAllocator: split / coalescing / alignment / exhaustion
    // ========================================================================
    FreeListAllocator freeList(1024);
    assert(freeList.GetCapacity() == 1024);
    assert(freeList.GetFreeBlockCount() == 1);
    void* freeA = freeList.Allocate(80, 16);
    void* freeB = freeList.Allocate(96, 64);
    void* freeC = freeList.Allocate(64, 32);
    assert(freeA != nullptr);
    assert(freeB != nullptr);
    assert(freeC != nullptr);
    assert(IsAligned(freeB, 64));
    assert(IsAligned(freeC, 32));
    assert(freeList.GetAllocationCount() == 3);
    assert(freeList.Allocate(0, 8) == nullptr);
    assert(freeList.Allocate(8, 3) == nullptr);
    assert(freeList.Allocate(2048, 8) == nullptr);

    freeList.Deallocate(freeB);
    freeList.Deallocate(freeA);
    freeList.Deallocate(freeC);
    assert(freeList.GetAllocationCount() == 0);
    assert(freeList.GetUsedMemory() == 0);
    assert(freeList.GetFreeBlockCount() == 1);
    assert(freeList.GetLargestFreeBlock() == freeList.GetCapacity());
    const std::size_t freeListPeak = freeList.GetPeakUsedMemory();
    freeList.Reset();
    assert(freeList.GetPeakUsedMemory() == freeListPeak);

    // ========================================================================
    // 6. PoolAllocator: alignment / exhaustion / reuse / statistics
    // ========================================================================
    PoolAllocator pool(24, 64, 3);
    assert(pool.GetSlotCount() == 3);
    assert(pool.GetAvailableSlotCount() == 3);
    void* poolA = pool.Allocate(24, 64);
    void* poolB = pool.Allocate(16, 32);
    void* poolC = pool.Allocate(24, 64);
    assert(poolA != nullptr);
    assert(poolB != nullptr);
    assert(poolC != nullptr);
    assert(IsAligned(poolA, 64));
    assert(IsAligned(poolB, 32));
    assert(pool.GetAllocationCount() == 3);
    assert(pool.GetUsedMemory() == pool.GetCapacity());
    assert(pool.Allocate(8, 8) == nullptr);
    assert(pool.Allocate(25, 8) == nullptr);
    assert(pool.Allocate(8, 3) == nullptr);
    pool.Deallocate(poolB);
    assert(pool.GetAvailableSlotCount() == 1);
    void* poolReused = pool.Allocate(16, 16);
    assert(poolReused == poolB);
    const std::size_t poolPeak = pool.GetPeakUsedMemory();
    pool.Reset();
    assert(pool.GetAllocationCount() == 0);
    assert(pool.GetAvailableSlotCount() == 3);
    assert(pool.GetPeakUsedMemory() == poolPeak);
    PoolAllocator invalidPool(32, 3, 4);
    assert(invalidPool.GetCapacity() == 0);
    assert(invalidPool.Allocate(8, 8) == nullptr);

    // ========================================================================
    // 7. MemoryTracker: Tag別集計、個別解放、Reset、リーク一覧
    // ========================================================================
    MemoryTracker& tracker = MemoryTracker::Get();
    const std::size_t initialLeaks = tracker.GetLeakCount();
    {
        HeapAllocator trackedHeap;
        TrackedAllocator tracked(trackedHeap, MemoryTag::Physics, "HeapAllocator");
        void* trackedA = tracked.AllocateAt(48, 16, __FILE__, __LINE__);
        void* trackedB = tracked.Allocate(64, 32);
        assert(trackedA != nullptr);
        assert(trackedB != nullptr);
        assert(tracker.GetStatistics(MemoryTag::Physics).currentBytes >= 112);
        assert(tracker.GetLeakCount() == initialLeaks + 2);
        tracked.Deallocate(trackedA);
        assert(tracker.GetLeakCount() == initialLeaks + 1);
        tracked.Reset();
        assert(tracker.GetLeakCount() == initialLeaks);
    }

    // ========================================================================
    // 8. Raven::Vector: Allocator連携 / growth / copy / move / Iterator
    // ========================================================================
    HeapAllocator vectorHeap;
    {
        Vector<int> numbers(vectorHeap);
        assert(numbers.empty());
        assert(numbers.size() == 0);
        assert(numbers.GetAllocator() == &vectorHeap);
        numbers.reserve(2);
        numbers.push_back(3);
        numbers.emplace_back(7);
        numbers.push_back(11);
        assert(numbers.size() == 3);
        assert(numbers.capacity() >= 3);
        assert(numbers.front() == 3);
        assert(numbers.back() == 11);
        int sum = 0;
        for (int value : numbers)
        {
            sum += value;
        }
        assert(sum == 21);
        Vector<int> copied(numbers);
        assert(copied.size() == 3);
        Vector<int> moved(std::move(copied));
        assert(moved.size() == 3);
        moved.pop_back();
        moved.resize(4, 42);
        assert(moved[2] == 42);
        assert(moved[3] == 42);
        moved.clear();
        assert(moved.empty());

        Vector<std::unique_ptr<int>> owned(vectorHeap);
        owned.emplace_back(std::make_unique<int>(99));
        owned.push_back(std::make_unique<int>(100));
        assert(*owned[0] == 99);
        assert(*owned[1] == 100);

        struct alignas(64) AlignedValue { int value = 0; };
        Vector<AlignedValue> aligned(vectorHeap);
        aligned.emplace_back();
        assert(IsAligned(aligned.data(), 64));

        Vector<int> emptyCopy(numbers);
        assert(emptyCopy.size() == numbers.size());
        bool overflowCaught = false;
        try
        {
            numbers.reserve((std::numeric_limits<std::size_t>::max)());
        }
        catch (const std::length_error&)
        {
            overflowCaught = true;
        }
        catch (const std::bad_alloc&)
        {
            overflowCaught = true;
        }
        assert(overflowCaught);
    }
    assert(vectorHeap.GetAllocationCount() == 0);

    // ========================================================================
    // 9. Raven::Vector: non-trivial lifetime / growth / allocation failure
    // ========================================================================
    static_assert(std::is_move_constructible_v<Vector<std::unique_ptr<int>>>);
    VectorLifecycleValue::live = 0;
    VectorLifecycleValue::copies = 0;
    VectorLifecycleValue::moves = 0;
    {
        Vector<VectorLifecycleValue> lifecycle(vectorHeap);
        lifecycle.reserve(2);
        lifecycle.emplace_back(1);
        lifecycle.emplace_back(2);
        assert(VectorLifecycleValue::live == 2);
        const std::size_t originalCapacity = lifecycle.capacity();
        lifecycle.emplace_back(3);
        assert(lifecycle.capacity() > originalCapacity);
        assert(VectorLifecycleValue::live == 3);
        assert(lifecycle[0].value == 1 && lifecycle[2].value == 3);
        assert(VectorLifecycleValue::moves >= 2);
        {
            Vector<VectorLifecycleValue> copy(lifecycle);
            assert(VectorLifecycleValue::live == 6);
            assert(VectorLifecycleValue::copies >= 3);
        }
        assert(VectorLifecycleValue::live == 3);
        lifecycle.pop_back();
        assert(VectorLifecycleValue::live == 2);
        lifecycle.clear();
        assert(VectorLifecycleValue::live == 0);
    }
    assert(VectorLifecycleValue::live == 0);
    {
        FailingVectorAllocator failing(vectorHeap);
        Vector<int> checked(failing);
        checked.reserve(2);
        checked.push_back(5);
        checked.push_back(9);
        const std::size_t beforeCapacity = checked.capacity();
        const int* beforeData = checked.data();
        failing.SetReject(true);
        bool allocationFailed = false;
        try
        {
            checked.reserve(beforeCapacity + 1);
        }
        catch (const std::bad_alloc&)
        {
            allocationFailed = true;
        }
        assert(allocationFailed);
        assert(checked.size() == 2);
        assert(checked.capacity() == beforeCapacity);
        assert(checked.data() == beforeData);
        assert(checked[0] == 5 && checked[1] == 9);
        assert(std::distance(checked.cbegin(), checked.cend()) == 2);
        failing.SetReject(false);
        checked.emplace_back(13);
        assert(checked.back() == 13);
    }
    assert(vectorHeap.GetAllocationCount() == 0);

    // ========================================================================
    // 10. SmallVector: inline / overflow / move / destruction / alignment
    // ========================================================================
    HeapAllocator smallHeap;
    {
        SmallVector<int, 2> small(smallHeap);
        assert(small.capacity() == 2);
        assert(small.UsingInlineStorage());
        assert(smallHeap.GetAllocationCount() == 0);
        small.emplace_back(10);
        small.push_back(20);
        assert(smallHeap.GetAllocationCount() == 0);
        small.push_back(30);
        assert(small.UsingInlineStorage() == false);
        assert(smallHeap.GetAllocationCount() == 1);
        assert(small[0] == 10 && small[1] == 20 && small[2] == 30);
        SmallVector<int, 2> moved(std::move(small));
        assert(moved.size() == 3);
        assert(moved.back() == 30);
        assert(small.empty());
        assert(small.UsingInlineStorage());
        moved.pop_back();
        assert(moved.size() == 2);
    }
    assert(smallHeap.GetAllocationCount() == 0);
    {
        SmallVector<std::unique_ptr<int>, 2> owned(smallHeap);
        owned.emplace_back(std::make_unique<int>(1));
        owned.emplace_back(std::make_unique<int>(2));
        SmallVector<std::unique_ptr<int>, 2> inlineMoved(std::move(owned));
        assert(inlineMoved.size() == 2);
        assert(*inlineMoved[0] == 1);
        inlineMoved.emplace_back(std::make_unique<int>(3));
        assert(*inlineMoved[2] == 3);
    }
    assert(smallHeap.GetAllocationCount() == 0);
    {
        struct alignas(64) AlignedSmall { int value = 0; };
        SmallVector<AlignedSmall, 1> aligned(smallHeap);
        aligned.emplace_back();
        assert(IsAligned(aligned.data(), 64));
        aligned.emplace_back();
        assert(IsAligned(aligned.data(), 64));
    }
    assert(smallHeap.GetAllocationCount() == 0);
    {
        FailingVectorAllocator failing(smallHeap);
        SmallVector<int, 2> checked(failing);
        checked.push_back(4);
        checked.push_back(8);
        failing.SetReject(true);
        bool failed = false;
        try
        {
            checked.emplace_back(12);
        }
        catch (const std::bad_alloc&)
        {
            failed = true;
        }
        assert(failed);
        assert(checked.size() == 2);
        assert(checked.UsingInlineStorage());
        assert(checked[0] == 4 && checked[1] == 8);
    }
    assert(smallHeap.GetAllocationCount() == 0);

    // ========================================================================
    // 11. FixedVector: no allocation / overflow / lifetime / alignment
    // ========================================================================
    const std::size_t heapAllocationsBeforeFixed = smallHeap.GetAllocationCount();
    {
        FixedVector<int, 2> fixed;
        assert(fixed.empty());
        assert(fixed.capacity() == 2);
        assert(fixed.try_push_back(3));
        assert(fixed.try_emplace_back(7) != nullptr);
        assert(fixed.full());
        assert(fixed.try_push_back(11) == false);
        assert(fixed.size() == 2);
        bool overflowRejected = false;
        try
        {
            fixed.emplace_back(11);
        }
        catch (const std::length_error&)
        {
            overflowRejected = true;
        }
        assert(overflowRejected);
        assert(fixed.front() == 3 && fixed.back() == 7);
        FixedVector<int, 2> moved(std::move(fixed));
        assert(moved.size() == 2);
        assert(fixed.empty());
        moved.pop_back();
        assert(moved.size() == 1);
        moved.clear();
        assert(moved.empty());
    }
    assert(smallHeap.GetAllocationCount() == heapAllocationsBeforeFixed);
    {
        FixedVector<VectorLifecycleValue, 3> lifecycle;
        lifecycle.emplace_back(1);
        lifecycle.emplace_back(2);
        assert(VectorLifecycleValue::live == 2);
        lifecycle.clear();
        assert(VectorLifecycleValue::live == 0);
    }
    {
        struct alignas(64) AlignedFixed { int value = 0; };
        FixedVector<AlignedFixed, 2> aligned;
        aligned.emplace_back();
        assert(IsAligned(aligned.data(), 64));
    }
    {
        FixedVector<std::unique_ptr<int>, 2> owned;
        owned.emplace_back(std::make_unique<int>(4));
        owned.emplace_back(std::make_unique<int>(8));
        assert(*owned[1] == 8);
    }

    // ========================================================================
    // 12. Span: 非所有View / const / 空領域 / subspan / Vector連携
    // ========================================================================
    Span<int> emptySpan;
    assert(emptySpan.empty());
    assert(emptySpan.begin() == emptySpan.end());
    int rawValues[] = { 2, 4, 6, 8 };
    Span<int> rawSpan(rawValues);
    assert(rawSpan.size() == 4);
    rawSpan[1] = 5;
    assert(rawValues[1] == 5);
    Span<const int> readOnlySpan(rawValues);
    assert(readOnlySpan[1] == 5);
    const auto middle = rawSpan.subspan(1, 2);
    assert(middle.size() == 2 && middle[0] == 5 && middle[1] == 6);
    assert(rawSpan.subspan(rawSpan.size(), 0).empty());
    {
        Vector<int> owned(vectorHeap);
        owned.push_back(12);
        owned.push_back(24);
        Span<int> view(owned);
        assert(view.data() == owned.data());
        assert(view[1] == 24);
        const Vector<int>& constOwned = owned;
        Span<const int> constView(constOwned);
        assert(constView[0] == 12);
        // 再確保するとViewは無効になるため、ここでは要素を追加しません。
    }

    // ========================================================================
    // 13. Array: 固定長領域 / Span連携 / 範囲検査
    // ========================================================================
    Array<int, 3> array{ 1, 2, 3 };
    assert(array.size() == 3);
    array.fill(9);
    assert(array[0] == 9 && array[2] == 9);
    Span<int> arrayView(array);
    arrayView[1] = 7;
    assert(array[1] == 7);
    const Array<int, 3>& constArray = array;
    Span<const int> constArrayView(constArray);
    assert(constArrayView[1] == 7);
    Array<int, 0> zeroArray;
    assert(zeroArray.empty());
    bool arrayOutOfRange = false;
    try
    {
        (void)array.at(3);
    }
    catch (const std::out_of_range&)
    {
        arrayOutOfRange = true;
    }
    assert(arrayOutOfRange);

    // ========================================================================
    // 14. RingBuffer: FIFO / wrap / full / move-only / lifetime
    // ========================================================================
    {
        RingBuffer<int, 3> queue;
        assert(queue.empty());
        queue.push_back(1);
        queue.push_back(2);
        queue.push_back(3);
        assert(queue.full());
        assert(queue.try_push_back(4) == false);
        bool ringOverflow = false;
        try
        {
            queue.emplace_back(4);
        }
        catch (const std::length_error&)
        {
            ringOverflow = true;
        }
        assert(ringOverflow);
        queue.pop_front();
        queue.push_back(4);
        assert(queue[0] == 2 && queue[1] == 3 && queue[2] == 4);
        assert(queue.front() == 2 && queue.back() == 4);
        RingBuffer<int, 3> moved(std::move(queue));
        assert(queue.empty());
        assert(moved.size() == 3 && moved[2] == 4);
        moved.clear();
        assert(moved.empty());
    }
    {
        VectorLifecycleValue::live = 0;
        RingBuffer<VectorLifecycleValue, 2> lifecycle;
        lifecycle.emplace_back(10);
        lifecycle.emplace_back(20);
        assert(VectorLifecycleValue::live == 2);
        lifecycle.pop_front();
        assert(VectorLifecycleValue::live == 1);
        lifecycle.emplace_back(30);
        assert(lifecycle[0].value == 20 && lifecycle[1].value == 30);
        lifecycle.clear();
        assert(VectorLifecycleValue::live == 0);
    }
    {
        RingBuffer<std::unique_ptr<int>, 2> owned;
        owned.emplace_back(std::make_unique<int>(7));
        owned.emplace_back(std::make_unique<int>(9));
        assert(*owned.front() == 7);
        owned.pop_front();
        owned.emplace_back(std::make_unique<int>(11));
        assert(*owned.back() == 11);
    }
    {
        struct alignas(64) AlignedRing { int value = 0; };
        RingBuffer<AlignedRing, 2> aligned;
        aligned.emplace_back();
        assert(IsAligned(&aligned.front(), 64));
    }

    // ========================================================================
    // 15. BitSet: 境界bit / 全bit操作 / 余剰bit / 0bit
    // ========================================================================
    {
        BitSet<130> bits;
        assert(bits.size() == 130 && bits.none());
        bits.set(0).set(63).set(64).set(129);
        assert(bits.count() == 4);
        assert(bits.test(63) && bits.test(64) && bits[129]);
        bits.reset(63);
        bits.flip(64);
        assert(bits.count() == 2);
        bits.set();
        assert(bits.all() && bits.count() == 130);
        bits.flip();
        assert(bits.none());
        bits.flip(129);
        assert(bits.count() == 1);
        bits.reset();
        assert(bits.none());
        bool invalidBit = false;
        try
        {
            (void)bits.test(130);
        }
        catch (const std::out_of_range&)
        {
            invalidBit = true;
        }
        assert(invalidBit);
    }
    {
        BitSet<64> exactWord;
        exactWord.set();
        assert(exactWord.all() && exactWord.count() == 64);
        BitSet<1> oneBit;
        oneBit.set();
        assert(oneBit.all() && oneBit.count() == 1);
        BitSet<0> zeroBits;
        assert(zeroBits.empty() && zeroBits.none() && zeroBits.all());
        zeroBits.set().flip().reset();
        assert(zeroBits.count() == 0);
    }

    // ========================================================================
    // 16. HandlePool: 世代管理 / Slot再利用 / アドレス安定性 / move-only
    // ========================================================================
    {
        HandlePool<std::unique_ptr<int>> pool;
        const auto first = pool.emplace(std::make_unique<int>(10));
        auto* firstAddress = pool.get(first);
        assert(firstAddress != nullptr && **firstAddress == 10);
        for (int index = 0; index < 32; ++index)
        {
            pool.emplace(std::make_unique<int>(index));
        }
        assert(pool.get(first) == firstAddress);
        assert(pool.erase(first));
        assert(pool.get(first) == nullptr);
        assert(pool.erase(first) == false);
        const auto reused = pool.emplace(std::make_unique<int>(99));
        assert(reused.index == first.index);
        assert(reused.generation != first.generation);
        assert(pool.get(first) == nullptr && **pool.get(reused) == 99);
        const HandlePool<std::unique_ptr<int>>& constPool = pool;
        assert(constPool.contains(reused));
        pool.clear();
        assert(pool.empty() && pool.get(reused) == nullptr);
        const auto afterClear = pool.emplace(std::make_unique<int>(7));
        assert(pool.contains(afterClear));
        assert(pool.get(reused) == nullptr);
    }
    {
        VectorLifecycleValue::live = 0;
        HandlePool<VectorLifecycleValue> pool;
        const auto first = pool.emplace(42);
        assert(VectorLifecycleValue::live == 1);
        assert(pool.erase(first));
        assert(VectorLifecycleValue::live == 0);
        const auto second = pool.emplace(64);
        assert(VectorLifecycleValue::live == 1);
        assert(pool.get(first) == nullptr && pool.get(second)->value == 64);
        pool.clear();
        assert(VectorLifecycleValue::live == 0);
    }
    {
        struct alignas(64) AlignedHandle { int value = 0; };
        HandlePool<AlignedHandle> pool;
        const auto handle = pool.emplace();
        assert(IsAligned(pool.get(handle), 64));
    }

    // ========================================================================
    // 17. SparseSet: 重複拒否 / swap-erase / 疎なID / 再挿入
    // ========================================================================
    {
        SparseSet ids;
        assert(ids.empty());
        ids.reserve(8, 128);
        assert(ids.insert(4));
        assert(ids.insert(100));
        assert(ids.insert(7));
        assert(ids.insert(4) == false);
        assert(ids.size() == 3);
        assert(ids.contains(100) && ids.contains(7));
        assert(ids.erase(4));
        assert(ids.contains(4) == false);
        assert(ids.contains(7) && ids.contains(100));
        assert(ids.size() == 2);
        assert(ids.erase(4) == false);
        assert(ids.insert(4));
        std::size_t visited = 0;
        for (std::uint32_t id : ids)
        {
            assert(ids.contains(id));
            ++visited;
        }
        assert(visited == ids.size());
        ids.clear();
        assert(ids.empty() && ids.contains(100) == false);
        assert(ids.insert(100));
        assert(ids[0] == 100);
    }

    // ========================================================================
    // 18. STLAllocatorAdapter: std::vectorの確保元をFrameAllocatorへ差し替える
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
