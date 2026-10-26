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

    explicit SmallVector(Allocator& allocator) noexcept;

    ~SmallVector();

    SmallVector(const SmallVector&) = delete;
    SmallVector& operator=(const SmallVector&) = delete;
    SmallVector& operator=(SmallVector&&) = delete;

    SmallVector(SmallVector&& other);

    [[nodiscard]] size_type size() const noexcept;
    [[nodiscard]] size_type capacity() const noexcept;
    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] bool UsingInlineStorage() const noexcept;
    [[nodiscard]] T* data() noexcept;
    [[nodiscard]] const T* data() const noexcept;
    [[nodiscard]] iterator begin() noexcept;
    [[nodiscard]] const_iterator begin() const noexcept;
    [[nodiscard]] const_iterator cbegin() const noexcept;
    [[nodiscard]] iterator end() noexcept;
    [[nodiscard]] const_iterator end() const noexcept;

    [[nodiscard]] const_iterator cend() const noexcept;

    T& operator[](size_type index) noexcept;
    const T& operator[](size_type index) const noexcept;
    T& front() noexcept;
    const T& front() const noexcept;
    T& back() noexcept;
    const T& back() const noexcept;

    template<typename... Args>
    T& emplace_back(Args&&... args);

    void push_back(const T& value);
    void push_back(T&& value);

    void pop_back();

    void clear() noexcept;

    void reserve(size_type count);

    [[nodiscard]] Allocator* GetAllocator() const noexcept;

private:
    using Storage = std::aligned_storage_t<sizeof(T), alignof(T)>;

    [[nodiscard]] T* InlineData() noexcept;

    [[nodiscard]] bool UsingInline() const noexcept;

    static void DestroyRange(T* data, size_type count) noexcept;

    void ReleaseOverflow() noexcept;

    void Grow(size_type required, T* pending = nullptr);

    Allocator* m_Allocator;
    Storage m_Inline[N];
    T* m_Data;
    size_type m_Size = 0;
    size_type m_Capacity = N;
};

} // namespace Raven

#include "Raven/Core/Containers/SmallVector.inl"
