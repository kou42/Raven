#pragma once

#include <cstddef>

namespace Raven
{

// エンジン内のAllocatorが共通で満たす最小インターフェースです。
//
// 重要な点:
// - Allocate() は必ず指定alignmentを満たすアドレスを返します。
// - Deallocate() の実際の意味はAllocatorごとに異なります。
//   LinearAllocatorのように個別解放を行わないAllocatorではno-opになります。
// - Reset() はAllocator全体を一括再利用可能な状態へ戻します。
//
// この共通化により、将来PoolAllocator / FreeListAllocatorを追加しても
// 利用側はAllocatorの種類を過度に意識せず扱えるようにします。
class Allocator
{
public:
    virtual ~Allocator() = default;

    virtual void* Allocate(std::size_t size, std::size_t alignment = alignof(std::max_align_t)) = 0;
    virtual void Deallocate(void* memory) = 0;
    virtual void Reset() = 0;

    [[nodiscard]] virtual std::size_t GetCapacity() const = 0;
    [[nodiscard]] virtual std::size_t GetUsedMemory() const = 0;
    [[nodiscard]] virtual std::size_t GetAllocationCount() const = 0;
};

}
