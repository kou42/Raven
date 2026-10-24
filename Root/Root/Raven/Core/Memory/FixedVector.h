#pragma once

#include <cassert>
#include <cstddef>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace Raven
{

// 最大N要素をオブジェクト内に直接保持する固定容量コンテナです。
// Allocatorやheapは一切使用しません。容量超過はtry_emplace_back()でfalseを返し、
// emplace_back()ではstd::length_errorを送出します。
template<typename T, std::size_t N>
class FixedVector
{
    static_assert(N > 0, "FixedVector requires positive capacity");

public:
    using value_type = T;
    using size_type = std::size_t;
    using iterator = T*;
    using const_iterator = const T*;

    FixedVector() noexcept = default;
    FixedVector(const FixedVector&) = delete;
    FixedVector& operator=(const FixedVector&) = delete;
    FixedVector& operator=(FixedVector&&) = delete;

    FixedVector(FixedVector&& other) noexcept(std::is_nothrow_move_constructible_v<T>);

    ~FixedVector() noexcept;

    [[nodiscard]] constexpr size_type capacity() const noexcept;
    [[nodiscard]] size_type size() const noexcept;
    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] bool full() const noexcept;
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
    T* try_emplace_back(Args&&... args);

    template<typename... Args>
    T& emplace_back(Args&&... args);

    bool try_push_back(const T& value);
    bool try_push_back(T&& value);
    void push_back(const T& value);
    void push_back(T&& value);

    void pop_back() noexcept;

    void clear() noexcept;

private:
    using Storage = std::aligned_storage_t<sizeof(T), alignof(T)>;
    Storage m_Storage[N];
    size_type m_Size = 0;
};

} // namespace Raven

#include "Raven/Core/Memory/FixedVector.inl"
