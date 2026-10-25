#pragma once

#include <cassert>
#include <cstddef>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace Raven
{

// 固定容量・非上書きのFIFOコンテナです。
// 空きSlotだけを構築し、取り出した要素は直ちに破棄します。
// Allocatorを使わず、メモリ使用量はコンパイル時に確定します。
template<typename T, std::size_t N>
class RingBuffer
{
    static_assert(N > 0, "RingBuffer requires positive capacity");
    static_assert(std::is_nothrow_destructible_v<T>, "RingBuffer requires noexcept destructible elements");

public:
    using value_type = T;
    using size_type = std::size_t;

    RingBuffer() noexcept = default;
    RingBuffer(const RingBuffer&) = delete;
    RingBuffer& operator=(const RingBuffer&) = delete;
    RingBuffer& operator=(RingBuffer&&) = delete;
    RingBuffer(RingBuffer&& other) noexcept(std::is_nothrow_move_constructible_v<T>);
    ~RingBuffer() noexcept;

    [[nodiscard]] constexpr size_type capacity() const noexcept;
    [[nodiscard]] size_type size() const noexcept;
    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] bool full() const noexcept;

    template<typename... Args>
    T* try_emplace_back(Args&&... args);

    template<typename... Args>
    T& emplace_back(Args&&... args);

    bool try_push_back(const T& value);
    bool try_push_back(T&& value);
    void push_back(const T& value);
    void push_back(T&& value);

    T& front() noexcept;
    const T& front() const noexcept;
    T& back() noexcept;
    const T& back() const noexcept;
    T& operator[](size_type index) noexcept;
    const T& operator[](size_type index) const noexcept;

    void pop_front() noexcept;
    void clear() noexcept;

private:
    using Storage = std::aligned_storage_t<sizeof(T), alignof(T)>;
    [[nodiscard]] size_type PhysicalIndex(size_type logicalIndex) const noexcept;
    [[nodiscard]] T* ElementAt(size_type index) noexcept;
    [[nodiscard]] const T* ElementAt(size_type index) const noexcept;

    Storage m_Storage[N];
    size_type m_Head = 0;
    size_type m_Size = 0;
};

} // namespace Raven

#include "Raven/Core/Containers/RingBuffer.inl"
