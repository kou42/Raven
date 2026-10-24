#pragma once

#include "Raven/Core/Memory/Allocator.h"

#include <cassert>
#include <cstddef>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace Raven
{

// N個まではオブジェクト内のinline領域を使い、超過時のみAllocatorへ確保します。
// AllocatorはSmallVectorより長生きし、個別Deallocateをサポートする必要があります。
// Phase 4初期版では、例外安全性を明確にするためcopyと代入を禁止します。
template<typename T, std::size_t N>
class SmallVector
{
    static_assert(N > 0, "SmallVector requires positive inline capacity");
    static_assert(std::is_nothrow_move_constructible_v<T> || std::is_copy_constructible_v<T>,
        "SmallVector requires nothrow move or copy construction for safe growth");

public:
    using value_type = T;
    using size_type = std::size_t;
    using iterator = T*;
    using const_iterator = const T*;

    explicit SmallVector(Allocator& allocator) noexcept
        : m_Allocator(&allocator), m_Data(InlineData())
    {
    }

    ~SmallVector()
    {
        clear();
        ReleaseOverflow();
    }

    SmallVector(const SmallVector&) = delete;
    SmallVector& operator=(const SmallVector&) = delete;
    SmallVector& operator=(SmallVector&&) = delete;

    SmallVector(SmallVector&& other)
        : m_Allocator(other.m_Allocator), m_Data(InlineData())
    {
        if (other.UsingInline() == false)
        {
            // heap領域は所有権を移すだけで、要素の移動コストを発生させません。
            m_Data = other.m_Data;
            m_Size = other.m_Size;
            m_Capacity = other.m_Capacity;
            other.m_Data = other.InlineData();
            other.m_Size = 0;
            other.m_Capacity = N;
        }
        else
        {
            // 失敗時もotherを破壊せず、構築済み要素だけを巻き戻します。
            std::size_t built = 0;
            try
            {
                for (; built < other.m_Size; ++built)
                {
                    ::new (static_cast<void*>(m_Data + built))
                        T(std::move_if_noexcept(other.m_Data[built]));
                }
            }
            catch (...)
            {
                DestroyRange(m_Data, built);
                throw;
            }
            m_Size = built;
            other.clear();
        }
    }

    [[nodiscard]] size_type size() const noexcept { return m_Size; }
    [[nodiscard]] size_type capacity() const noexcept { return m_Capacity; }
    [[nodiscard]] bool empty() const noexcept { return m_Size == 0; }
    [[nodiscard]] bool UsingInlineStorage() const noexcept { return UsingInline(); }
    [[nodiscard]] T* data() noexcept { return m_Data; }
    [[nodiscard]] const T* data() const noexcept { return m_Data; }
    [[nodiscard]] iterator begin() noexcept { return m_Data; }
    [[nodiscard]] const_iterator begin() const noexcept { return m_Data; }
    [[nodiscard]] iterator end() noexcept { return m_Data + m_Size; }
    [[nodiscard]] const_iterator end() const noexcept { return m_Data + m_Size; }

    T& operator[](size_type index) noexcept
    {
        assert(index < m_Size);
        return m_Data[index];
    }
    const T& operator[](size_type index) const noexcept
    {
        assert(index < m_Size);
        return m_Data[index];
    }
    T& front() noexcept { assert(m_Size > 0); return m_Data[0]; }
    const T& front() const noexcept { assert(m_Size > 0); return m_Data[0]; }
    T& back() noexcept { assert(m_Size > 0); return m_Data[m_Size - 1]; }
    const T& back() const noexcept { assert(m_Size > 0); return m_Data[m_Size - 1]; }

    template<typename... Args>
    T& emplace_back(Args&&... args)
    {
        if (m_Size == m_Capacity)
        {
            // 先に新要素を一時構築し、既存要素の移設中に参照が無効化されるのを防ぎます。
            // move-onlyでthrowする型はstatic_assertで除外しています。
            T pending(std::forward<Args>(args)...);
            Grow(m_Size + 1);
            ::new (static_cast<void*>(m_Data + m_Size)) T(std::move(pending));
        }
        else
        {
            ::new (static_cast<void*>(m_Data + m_Size)) T(std::forward<Args>(args)...);
        }
        return m_Data[m_Size++];
    }

    void push_back(const T& value) { emplace_back(value); }
    void push_back(T&& value) { emplace_back(std::move(value)); }

    void pop_back()
    {
        assert(m_Size > 0);
        --m_Size;
        m_Data[m_Size].~T();
    }

    void clear() noexcept
    {
        DestroyRange(m_Data, m_Size);
        m_Size = 0;
    }

    void reserve(size_type count)
    {
        if (count > m_Capacity)
        {
            Grow(count);
        }
    }

    [[nodiscard]] Allocator* GetAllocator() const noexcept { return m_Allocator; }

private:
    using Storage = std::aligned_storage_t<sizeof(T), alignof(T)>;

    [[nodiscard]] T* InlineData() noexcept
    {
        return reinterpret_cast<T*>(m_Inline);
    }

    [[nodiscard]] bool UsingInline() const noexcept
    {
        return m_Data == reinterpret_cast<const T*>(m_Inline);
    }

    static void DestroyRange(T* data, size_type count) noexcept
    {
        while (count > 0)
        {
            data[--count].~T();
        }
    }

    void ReleaseOverflow() noexcept
    {
        if (UsingInline() == false)
        {
            m_Allocator->Deallocate(m_Data);
        }
    }

    void Grow(size_type required)
    {
        if (required > (std::numeric_limits<size_type>::max)() / sizeof(T))
        {
            throw std::length_error("SmallVector capacity overflow");
        }

        const size_type maxCount = (std::numeric_limits<size_type>::max)() / sizeof(T);
        size_type next = m_Capacity;
        while (next < required)
        {
            if (next > maxCount / 2)
            {
                next = required;
                break;
            }
            next *= 2;
        }

        void* raw = m_Allocator->Allocate(next * sizeof(T), alignof(T));
        if (raw == nullptr)
        {
            throw std::bad_alloc{};
        }

        T* destination = static_cast<T*>(raw);
        size_type built = 0;
        try
        {
            for (; built < m_Size; ++built)
            {
                ::new (static_cast<void*>(destination + built))
                    T(std::move_if_noexcept(m_Data[built]));
            }
        }
        catch (...)
        {
            DestroyRange(destination, built);
            m_Allocator->Deallocate(destination);
            throw;
        }

        // 全要素の移設が成功してから旧領域を破棄し、所有権を切り替えます。
        DestroyRange(m_Data, m_Size);
        ReleaseOverflow();
        m_Data = destination;
        m_Capacity = next;
    }

    Allocator* m_Allocator;
    Storage m_Inline[N];
    T* m_Data;
    size_type m_Size = 0;
    size_type m_Capacity = N;
};

} // namespace Raven
