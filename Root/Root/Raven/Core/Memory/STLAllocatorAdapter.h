#pragma once

#include "Raven/Core/Memory/Allocator.h"

#include <cstddef>
#include <new>
#include <type_traits>

namespace Raven
{

// ============================================================================
// STLAllocatorAdapter
// ============================================================================
// Raven独自Allocatorをstd::vectorなどのSTLコンテナから利用するためのAdapterです。
//
// 重要:
// - Adapter自身はAllocatorを所有しません。
// - コンテナよりAllocatorの寿命が長いことが前提です。
// - allocate()失敗時はSTL allocator契約に合わせてstd::bad_allocを送出します。
// - deallocate()は背後のAllocatorへ委譲します。Frame/LinearAllocatorではno-opです。
//
// これにより、既存コードを一度に独自Containerへ置き換えず、
// std::vectorの使い勝手を維持したままメモリ確保元だけを段階的に差し替えられます。
template<typename T>
class STLAllocatorAdapter
{
public:
    using value_type = T;

    STLAllocatorAdapter() noexcept = default;

    explicit STLAllocatorAdapter(Allocator& allocator) noexcept
        : m_Allocator(&allocator)
    {
    }

    template<typename U>
    STLAllocatorAdapter(const STLAllocatorAdapter<U>& other) noexcept
        : m_Allocator(other.GetAllocator())
    {
    }

    [[nodiscard]] T* allocate(std::size_t count)
    {
        if (m_Allocator == nullptr)
        {
            throw std::bad_alloc{};
        }

        if (count == 0)
        {
            return nullptr;
        }

        if (count > static_cast<std::size_t>(-1) / sizeof(T))
        {
            throw std::bad_alloc{};
        }

        void* memory = m_Allocator->Allocate(count * sizeof(T), alignof(T));
        if (memory == nullptr)
        {
            throw std::bad_alloc{};
        }

        return static_cast<T*>(memory);
    }

    void deallocate(T* memory, std::size_t count) noexcept
    {
        static_cast<void>(count);

        if (m_Allocator == nullptr || memory == nullptr)
        {
            return;
        }

        m_Allocator->Deallocate(memory);
    }

    [[nodiscard]] Allocator* GetAllocator() const noexcept
    {
        return m_Allocator;
    }

    template<typename U>
    bool operator==(const STLAllocatorAdapter<U>& rhs) const noexcept
    {
        return m_Allocator == rhs.GetAllocator();
    }

    template<typename U>
    bool operator!=(const STLAllocatorAdapter<U>& rhs) const noexcept
    {
        return m_Allocator != rhs.GetAllocator();
    }

private:
    template<typename>
    friend class STLAllocatorAdapter;

    Allocator* m_Allocator = nullptr;
};

} // namespace Raven
