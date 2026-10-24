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

    FixedVector(FixedVector&& other) noexcept(std::is_nothrow_move_constructible_v<T>)
    {
        // Inline Storageのアドレスは移譲できないため、要素を個別に移動します。
        // 途中で例外が発生した場合も構築済み要素はこのオブジェクトのDestructorで破棄します。
        try
        {
            for (T& value : other)
            {
                emplace_back(std::move(value));
            }
        }
        catch (...)
        {
            // コンストラクタが例外終了するとFixedVectorのDestructorは呼ばれません。
            // ここで構築済み要素を破棄してリソースリークを防ぎます。
            clear();
            throw;
        }
        other.clear();
    }

    ~FixedVector() noexcept
    {
        clear();
    }

    [[nodiscard]] constexpr size_type capacity() const noexcept { return N; }
    [[nodiscard]] size_type size() const noexcept { return m_Size; }
    [[nodiscard]] bool empty() const noexcept { return m_Size == 0; }
    [[nodiscard]] bool full() const noexcept { return m_Size == N; }
    [[nodiscard]] T* data() noexcept { return reinterpret_cast<T*>(m_Storage); }
    [[nodiscard]] const T* data() const noexcept { return reinterpret_cast<const T*>(m_Storage); }

    [[nodiscard]] iterator begin() noexcept { return data(); }
    [[nodiscard]] const_iterator begin() const noexcept { return data(); }
    [[nodiscard]] const_iterator cbegin() const noexcept { return data(); }
    [[nodiscard]] iterator end() noexcept { return data() + m_Size; }
    [[nodiscard]] const_iterator end() const noexcept { return data() + m_Size; }
    [[nodiscard]] const_iterator cend() const noexcept { return data() + m_Size; }

    T& operator[](size_type index) noexcept
    {
        assert(index < m_Size);
        return data()[index];
    }
    const T& operator[](size_type index) const noexcept
    {
        assert(index < m_Size);
        return data()[index];
    }
    T& front() noexcept { assert(empty() == false); return data()[0]; }
    const T& front() const noexcept { assert(empty() == false); return data()[0]; }
    T& back() noexcept { assert(empty() == false); return data()[m_Size - 1]; }
    const T& back() const noexcept { assert(empty() == false); return data()[m_Size - 1]; }

    template<typename... Args>
    T* try_emplace_back(Args&&... args)
    {
        if (full())
        {
            return nullptr;
        }
        T* location = data() + m_Size;
        ::new (static_cast<void*>(location)) T(std::forward<Args>(args)...);
        ++m_Size;
        return location;
    }

    template<typename... Args>
    T& emplace_back(Args&&... args)
    {
        if (full())
        {
            throw std::length_error("FixedVector capacity exceeded");
        }
        return *try_emplace_back(std::forward<Args>(args)...);
    }

    bool try_push_back(const T& value) { return try_emplace_back(value) != nullptr; }
    bool try_push_back(T&& value) { return try_emplace_back(std::move(value)) != nullptr; }
    void push_back(const T& value) { emplace_back(value); }
    void push_back(T&& value) { emplace_back(std::move(value)); }

    void pop_back() noexcept
    {
        assert(empty() == false);
        --m_Size;
        data()[m_Size].~T();
    }

    void clear() noexcept
    {
        while (empty() == false)
        {
            pop_back();
        }
    }

private:
    using Storage = std::aligned_storage_t<sizeof(T), alignof(T)>;
    Storage m_Storage[N];
    size_type m_Size = 0;
};

} // namespace Raven
