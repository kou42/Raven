#include "Raven/Core/Memory/MemoryTracker.h"

#include <algorithm>
#include <cassert>
#include <limits>

namespace Raven
{

MemoryTracker& MemoryTracker::Get()
{
    static MemoryTracker tracker;
    return tracker;
}

void MemoryTracker::RecordAllocation(const MemoryAllocationRecord& record)
{
    if (record.memory == nullptr || static_cast<std::size_t>(record.tag) >= m_ByTag.size())
    {
        return;
    }

    std::lock_guard<std::mutex> lock(m_Mutex);
    assert(m_Records.find(record.memory) == m_Records.end() && "MemoryTracker: duplicate pointer");
    if (m_Records.find(record.memory) != m_Records.end())
    {
        return;
    }
    auto& tag = m_ByTag[static_cast<std::size_t>(record.tag)];
    assert(record.size <= (std::numeric_limits<std::size_t>::max)() - tag.currentBytes);
    assert(record.size <= (std::numeric_limits<std::size_t>::max)() - m_Total.currentBytes);
    m_Records.emplace(record.memory, record);
    tag.currentBytes += record.size;
    tag.peakBytes = std::max(tag.peakBytes, tag.currentBytes);
    ++tag.allocationCount;
    m_Total.currentBytes += record.size;
    m_Total.peakBytes = std::max(m_Total.peakBytes, m_Total.currentBytes);
    ++m_Total.allocationCount;
}

void MemoryTracker::RecordDeallocation(void* memory)
{
    if (memory == nullptr)
    {
        return;
    }

    std::lock_guard<std::mutex> lock(m_Mutex);
    const auto it = m_Records.find(memory);
    assert(it != m_Records.end() && "MemoryTracker: unknown pointer");
    if (it == m_Records.end())
    {
        return;
    }
    auto& tag = m_ByTag[static_cast<std::size_t>(it->second.tag)];
    tag.currentBytes -= it->second.size;
    --tag.allocationCount;
    m_Total.currentBytes -= it->second.size;
    --m_Total.allocationCount;
    m_Records.erase(it);
}

MemoryStatistics MemoryTracker::GetStatistics(MemoryTag tag) const
{
    std::lock_guard<std::mutex> lock(m_Mutex);
    const std::size_t index = static_cast<std::size_t>(tag);
    return index < m_ByTag.size() ? m_ByTag[index] : MemoryStatistics{};
}

MemoryStatistics MemoryTracker::GetTotalStatistics() const
{
    std::lock_guard<std::mutex> lock(m_Mutex);
    return m_Total;
}

std::vector<MemoryAllocationRecord> MemoryTracker::GetLiveAllocations() const
{
    std::lock_guard<std::mutex> lock(m_Mutex);
    std::vector<MemoryAllocationRecord> records;
    records.reserve(m_Records.size());
    for (const auto& entry : m_Records)
    {
        records.push_back(entry.second);
    }
    return records;
}

std::size_t MemoryTracker::GetLeakCount() const
{
    std::lock_guard<std::mutex> lock(m_Mutex);
    return m_Records.size();
}

void MemoryTracker::Reset()
{
    std::lock_guard<std::mutex> lock(m_Mutex);
    assert(m_Records.empty() && "MemoryTracker: cannot reset with live allocations");
    if (m_Records.empty() == false)
    {
        return;
    }
    m_ByTag = {};
    m_Total = {};
}

TrackedAllocator::TrackedAllocator(Allocator& backing, MemoryTag tag, const char* allocatorType)
    : m_Backing(backing), m_Tag(tag), m_AllocatorType(allocatorType)
{
}

TrackedAllocator::~TrackedAllocator()
{
    // backing allocatorの寿命はDecoratorより長いことを前提とします。
    Reset();
}

void* TrackedAllocator::Allocate(std::size_t size, std::size_t alignment)
{
    return AllocateAt(size, alignment, nullptr, 0);
}

void* TrackedAllocator::AllocateAt(std::size_t size, std::size_t alignment, const char* file, int line)
{
    void* memory = m_Backing.Allocate(size, alignment);
    if (memory == nullptr)
    {
        return nullptr;
    }

    // 追跡登録が失敗した場合も、元Allocatorの確保を取り残さないようにします。
    try
    {
        m_LivePointers.push_back(memory);
    }
    catch (...)
    {
        m_Backing.Deallocate(memory);
        throw;
    }
    try
    {
        MemoryTracker::Get().RecordAllocation({ memory, size, alignment, m_Tag, m_AllocatorType, file, line });
    }
    catch (...)
    {
        m_LivePointers.pop_back();
        m_Backing.Deallocate(memory);
        throw;
    }
    return memory;
}

void TrackedAllocator::Deallocate(void* memory)
{
    if (memory == nullptr)
    {
        return;
    }
    const auto it = std::find(m_LivePointers.begin(), m_LivePointers.end(), memory);
    assert(it != m_LivePointers.end() && "TrackedAllocator: unknown pointer");
    if (it == m_LivePointers.end())
    {
        return;
    }
    MemoryTracker::Get().RecordDeallocation(memory);
    m_LivePointers.erase(it);
    m_Backing.Deallocate(memory);
}

void TrackedAllocator::Reset()
{
    for (void* memory : m_LivePointers)
    {
        MemoryTracker::Get().RecordDeallocation(memory);
    }
    m_LivePointers.clear();
    m_Backing.Reset();
}

std::size_t TrackedAllocator::GetCapacity() const { return m_Backing.GetCapacity(); }
std::size_t TrackedAllocator::GetUsedMemory() const { return m_Backing.GetUsedMemory(); }
std::size_t TrackedAllocator::GetPeakUsedMemory() const { return m_Backing.GetPeakUsedMemory(); }
std::size_t TrackedAllocator::GetAllocationCount() const { return m_Backing.GetAllocationCount(); }

} // namespace Raven
