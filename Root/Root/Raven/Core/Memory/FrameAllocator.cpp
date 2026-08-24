#include "Raven/Core/Memory/FrameAllocator.h"

namespace Raven
{

FrameAllocator::FrameAllocator(std::size_t capacity)
    : m_Buffer(capacity > 0 ? std::make_unique<std::byte[]>(capacity) : nullptr)
    , m_LinearAllocator(m_Buffer.get(), capacity)
{
}

void* FrameAllocator::Allocate(std::size_t size, std::size_t alignment)
{
    return m_LinearAllocator.Allocate(size, alignment);
}

void FrameAllocator::Deallocate(void* memory)
{
    // FrameAllocatorもLinearAllocatorと同様に個別解放しません。
    // フレーム終了時のResetFrame()で一括して再利用可能にします。
    m_LinearAllocator.Deallocate(memory);
}

void FrameAllocator::Reset()
{
    m_LinearAllocator.Reset();
}

void FrameAllocator::ResetFrame()
{
    // API名で「フレーム境界での一括破棄」を読み取りやすくするため、
    // Reset()とは別に明示的なエントリポイントを用意しています。
    Reset();
}

std::size_t FrameAllocator::GetCapacity() const
{
    return m_LinearAllocator.GetCapacity();
}

std::size_t FrameAllocator::GetUsedMemory() const
{
    return m_LinearAllocator.GetUsedMemory();
}

std::size_t FrameAllocator::GetAllocationCount() const
{
    return m_LinearAllocator.GetAllocationCount();
}

}
