#pragma once

#include "Raven/Core/Memory/LinearAllocator.h"

#include <cstddef>
#include <memory>

namespace Raven
{

// 1フレーム中だけ必要な一時メモリをまとめて管理するAllocatorです。
//
// 内部ではLinearAllocatorを使用し、フレーム境界でResetFrame()を呼ぶことで
// そのフレーム中に確保した全メモリを一括で再利用可能にします。
//
// 重要:
// FrameAllocatorから取得したポインタはResetFrame()後に保持してはいけません。
// 次フレームのAllocate()で同じ領域が上書きされる可能性があります。
class FrameAllocator final : public Allocator
{
public:
    explicit FrameAllocator(std::size_t capacity);

    FrameAllocator(const FrameAllocator&) = delete;
    FrameAllocator& operator=(const FrameAllocator&) = delete;
    FrameAllocator(FrameAllocator&&) = delete;
    FrameAllocator& operator=(FrameAllocator&&) = delete;

    void* Allocate(std::size_t size, std::size_t alignment = alignof(std::max_align_t)) override;
    void Deallocate(void* memory) override;
    void Reset() override;

    // フレーム境界で呼ぶことをコード上でも明確にするための別名です。
    // 実処理はReset()と同じです。
    void ResetFrame();

    [[nodiscard]] std::size_t GetCapacity() const override;
    [[nodiscard]] std::size_t GetUsedMemory() const override;
    [[nodiscard]] std::size_t GetAllocationCount() const override;

private:
    // FrameAllocatorがbacking memoryの所有権を持ち、LinearAllocatorには
    // その領域への非所有ポインタだけを渡します。
    std::unique_ptr<std::byte[]> m_Buffer;
    LinearAllocator m_LinearAllocator;
};

}
