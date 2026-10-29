#include "Raven/Core/Memory/FreeListAllocator.h"

#include <algorithm>
#include <cassert>
#include <limits>

namespace Raven
{

FreeListAllocator::FreeListAllocator(std::size_t capacity)
    : m_Storage(capacity >= MinimumBlockSize ? std::make_unique<std::byte[]>(capacity) : nullptr)
    , m_Begin(m_Storage.get())
    , m_Capacity(m_Begin != nullptr ? capacity : 0)
{
    Reset();
}

void* FreeListAllocator::Allocate(std::size_t size, std::size_t alignment)
{
    if (size == 0 || alignment == 0 || (alignment & (alignment - 1)) != 0)
    {
        return nullptr;
    }

    // Headerはユーザーポインタ直前に配置し、解放時に元のブロック先頭を復元します。
    const std::size_t effectiveAlignment = std::max(alignment, alignof(AllocationHeader));
    // linkは「現在調べている空きブロックを指すポインタ」の格納場所です。
    // 最初はリストの先頭ポインタm_FreeList自身を指します。
    // 次のブロックへ進むときは、現在のブロックのnextメンバのアドレスを指します。
    // これにより先頭ブロックと途中のブロックを同じ代入(*link = next)で付け替えられ、
    // 直前のブロックをpreviousとして別途保持する必要がありません。
    FreeBlock** link = &m_FreeList;
    while (*link != nullptr)
    {
        // *linkは現在の空きブロックを指すFreeBlock*です。
        // link自体はFreeBlock**なので、ブロックを差し替える場合は*linkを書き換えます。
        FreeBlock* block = *link;
        const std::uintptr_t start = reinterpret_cast<std::uintptr_t>(block);
        if (start > (std::numeric_limits<std::uintptr_t>::max)() - sizeof(AllocationHeader))
        {
            return nullptr;
        }
        const std::uintptr_t afterHeader = start + sizeof(AllocationHeader);
        const std::size_t mask = effectiveAlignment - 1;
        const std::size_t adjustment = (effectiveAlignment - (afterHeader & mask)) & mask;
        const std::size_t headerOffset = sizeof(AllocationHeader) + adjustment;

        if (headerOffset <= block->size && size <= block->size - headerOffset)
        {
            const std::size_t required = headerOffset + size;
            // 分割後のFreeBlockを正しくalignするため、確保サイズを丸めます。
            const std::size_t remainder = required % alignof(FreeBlock);
            const std::size_t padding = remainder == 0 ? 0 : alignof(FreeBlock) - remainder;
            std::size_t consumed = required;
            if (padding <= block->size - required)
            {
                consumed += padding;
            }
            else
            {
                consumed = block->size;
            }

            if (block->size - consumed < MinimumBlockSize)
            {
                consumed = block->size;
            }

            FreeBlock* next = block->next;
            if (consumed < block->size)
            {
                auto* remaining = reinterpret_cast<FreeBlock*>(reinterpret_cast<std::byte*>(block) + consumed);
                remaining->size = block->size - consumed;
                remaining->next = next;
                next = remaining;
            }

            // 確保した領域は空きリストから取り除きます。
            // 分割した場合はnextが残余ブロックを、使い切った場合は元の次ブロックを指します。
            // linkは先頭なら&m_FreeList、途中なら&previousBlock->nextなので、
            // どちらの場合も一度の代入でリンクを更新できます。
            // 注意: この代入後、blockの領域は確保済みとして扱います。
            // 後続のHeader書き込みでは、退避済みのstartとheaderOffsetを使用します。
            *link = next;

            auto* header = reinterpret_cast<AllocationHeader*>(start + headerOffset - sizeof(AllocationHeader));
            header->size = consumed;
            header->adjustment = headerOffset;
            header->marker = AllocationMarker;
            m_UsedMemory += consumed;
            m_PeakUsedMemory = std::max(m_PeakUsedMemory, m_UsedMemory);
            ++m_AllocationCount;
            return reinterpret_cast<void*>(start + headerOffset);
        }

        // このブロックでは必要なサイズとalignmentを満たせませんでした。
        // 次回の*linkがblock->nextになるよう、リンクの格納場所を一つ進めます。
        // 空きブロックそのもののアドレスを移すのではない点がダブルポインタ方式の要点です。
        link = &block->next;
    }
    // 全空きブロックを調べても確保できなかった場合は失敗を通知します。
    return nullptr;
}

void FreeListAllocator::Deallocate(void* memory)
{
    if (memory == nullptr)
    {
        return;
    }

    const std::uintptr_t address = reinterpret_cast<std::uintptr_t>(memory);
    const std::uintptr_t begin = reinterpret_cast<std::uintptr_t>(m_Begin);
    // Arena外のアドレスは逆参照せずに拒否します。
    if (m_Begin == nullptr || address < begin + sizeof(AllocationHeader) || address >= begin + m_Capacity)
    {
        assert(false && "FreeListAllocator: pointer outside arena");
        return;
    }

    auto* header = reinterpret_cast<AllocationHeader*>(address - sizeof(AllocationHeader));
    if (header->marker != AllocationMarker || header->adjustment > address - begin ||
        header->size < header->adjustment || header->size > m_Capacity)
    {
        assert(false && "FreeListAllocator: invalid pointer or double free");
        return;
    }

    const std::uintptr_t start = address - header->adjustment;
    if (header->size > m_Capacity - (start - begin))
    {
        assert(false && "FreeListAllocator: invalid allocation size");
        return;
    }

    const std::size_t releasedSize = header->size;
    header->marker = 0;
    auto* freed = reinterpret_cast<FreeBlock*>(start);
    freed->size = releasedSize;

    // アドレス順に挿入し、隣接する空き領域を統合します。
    FreeBlock* previous = nullptr;
    FreeBlock* current = m_FreeList;
    while (current != nullptr && reinterpret_cast<std::uintptr_t>(current) < start)
    {
        previous = current;
        current = current->next;
    }
    freed->next = current;
    if (previous != nullptr)
    {
        previous->next = freed;
    }
    else
    {
        m_FreeList = freed;
    }

    if (current != nullptr && start + freed->size == reinterpret_cast<std::uintptr_t>(current))
    {
        freed->size += current->size;
        freed->next = current->next;
    }
    if (previous != nullptr &&
        reinterpret_cast<std::uintptr_t>(previous) + previous->size == start)
    {
        previous->size += freed->size;
        previous->next = freed->next;
    }

    m_UsedMemory -= releasedSize;
    --m_AllocationCount;
}

void FreeListAllocator::Reset()
{
    m_FreeList = nullptr;
    m_UsedMemory = 0;
    m_AllocationCount = 0;
    if (m_Begin != nullptr)
    {
        m_FreeList = reinterpret_cast<FreeBlock*>(m_Begin);
        m_FreeList->size = m_Capacity;
        m_FreeList->next = nullptr;
    }
}

std::size_t FreeListAllocator::GetCapacity() const { return m_Capacity; }
std::size_t FreeListAllocator::GetUsedMemory() const { return m_UsedMemory; }
std::size_t FreeListAllocator::GetPeakUsedMemory() const { return m_PeakUsedMemory; }
std::size_t FreeListAllocator::GetAllocationCount() const { return m_AllocationCount; }

std::size_t FreeListAllocator::GetFreeBlockCount() const
{
    std::size_t count = 0;
    for (const FreeBlock* block = m_FreeList; block != nullptr; block = block->next)
    {
        ++count;
    }
    return count;
}

std::size_t FreeListAllocator::GetLargestFreeBlock() const
{
    std::size_t largest = 0;
    for (const FreeBlock* block = m_FreeList; block != nullptr; block = block->next)
    {
        largest = std::max(largest, block->size);
    }
    return largest;
}

} // namespace Raven
