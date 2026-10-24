#pragma once

namespace Raven
{

template<typename T, std::size_t N>
FixedVector<T, N>::FixedVector(FixedVector&& other) noexcept(std::is_nothrow_move_constructible_v<T>)
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

template<typename T, std::size_t N>
FixedVector<T, N>::~FixedVector() noexcept
{
    clear();
}

template<typename T, std::size_t N>
[[nodiscard]] constexpr size_type FixedVector<T, N>::capacity() const noexcept
{ return N; }

template<typename T, std::size_t N>
[[nodiscard]] size_type FixedVector<T, N>::size() const noexcept
{ return m_Size; }

template<typename T, std::size_t N>
[[nodiscard]] bool FixedVector<T, N>::empty() const noexcept
{ return m_Size == 0; }

template<typename T, std::size_t N>
[[nodiscard]] bool FixedVector<T, N>::full() const noexcept
{ return m_Size == N; }

template<typename T, std::size_t N>
[[nodiscard]] T* FixedVector<T, N>::data() noexcept
{ return reinterpret_cast<T*>(m_Storage); }

template<typename T, std::size_t N>
[[nodiscard]] const T* FixedVector<T, N>::data() const noexcept
{ return reinterpret_cast<const T*>(m_Storage); }

template<typename T, std::size_t N>
[[nodiscard]] iterator FixedVector<T, N>::begin() noexcept
{ return data(); }

template<typename T, std::size_t N>
[[nodiscard]] const_iterator FixedVector<T, N>::begin() const noexcept
{ return data(); }

template<typename T, std::size_t N>
[[nodiscard]] const_iterator FixedVector<T, N>::cbegin() const noexcept
{ return data(); }

template<typename T, std::size_t N>
[[nodiscard]] iterator FixedVector<T, N>::end() noexcept
{ return data() + m_Size; }

template<typename T, std::size_t N>
[[nodiscard]] const_iterator FixedVector<T, N>::end() const noexcept
{ return data() + m_Size; }

template<typename T, std::size_t N>
[[nodiscard]] const_iterator FixedVector<T, N>::cend() const noexcept
{ return data() + m_Size; }

template<typename T, std::size_t N>
T& FixedVector<T, N>::operator[](size_type index) noexcept
{
    assert(index < m_Size);
    return data()[index];
}

template<typename T, std::size_t N>
const T& FixedVector<T, N>::operator[](size_type index) const noexcept
{
    assert(index < m_Size);
    return data()[index];
}

template<typename T, std::size_t N>
T& FixedVector<T, N>::front() noexcept
{ assert(empty() == false); return data()[0]; }

template<typename T, std::size_t N>
const T& FixedVector<T, N>::front() const noexcept
{ assert(empty() == false); return data()[0]; }

template<typename T, std::size_t N>
T& FixedVector<T, N>::back() noexcept
{ assert(empty() == false); return data()[m_Size - 1]; }

template<typename T, std::size_t N>
const T& FixedVector<T, N>::back() const noexcept
{ assert(empty() == false); return data()[m_Size - 1]; }

template<typename T, std::size_t N>
template<typename... Args>
T* FixedVector<T, N>::try_emplace_back(Args&&... args)
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

template<typename T, std::size_t N>
template<typename... Args>
T& FixedVector<T, N>::emplace_back(Args&&... args)
{
    if (full())
    {
        throw std::length_error("FixedVector capacity exceeded");
    }
    return *try_emplace_back(std::forward<Args>(args)...);
}

template<typename T, std::size_t N>
bool FixedVector<T, N>::try_push_back(const T& value)
{ return try_emplace_back(value) != nullptr; }

template<typename T, std::size_t N>
bool FixedVector<T, N>::try_push_back(T&& value)
{ return try_emplace_back(std::move(value)) != nullptr; }

template<typename T, std::size_t N>
void FixedVector<T, N>::push_back(const T& value)
{ emplace_back(value); }

template<typename T, std::size_t N>
void FixedVector<T, N>::push_back(T&& value)
{ emplace_back(std::move(value)); }

template<typename T, std::size_t N>
void FixedVector<T, N>::pop_back() noexcept
{
    assert(empty() == false);
    --m_Size;
    data()[m_Size].~T();
}

template<typename T, std::size_t N>
void FixedVector<T, N>::clear() noexcept
{
    while (empty() == false)
    {
        pop_back();
    }
}

} // namespace Raven
