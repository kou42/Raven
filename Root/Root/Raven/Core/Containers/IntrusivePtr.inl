#pragma once

#include <cassert>
#include <limits>

namespace Raven
{

inline void IntrusiveRefCounted::AddRef() const noexcept
{
    // 生存中の参照からの加算に同期は不要です。
    const auto previous = m_RefCount.fetch_add(1, std::memory_order_relaxed);
    assert(previous != (std::numeric_limits<std::uint32_t>::max)());
}

inline void IntrusiveRefCounted::ReleaseRef() const noexcept
{
    // 最終所有者はacquire同期を行ってからオブジェクトを破棄します。
    const auto previous = m_RefCount.fetch_sub(1, std::memory_order_acq_rel);
    assert(previous > 0);
    if (previous == 1)
    {
        delete this;
    }
}

inline std::uint32_t IntrusiveRefCounted::GetRefCount() const noexcept
{
    return m_RefCount.load(std::memory_order_relaxed);
}

template<typename T>
IntrusivePtr<T>::IntrusivePtr(std::nullptr_t) noexcept
{
}

template<typename T>
IntrusivePtr<T>::IntrusivePtr(T* pointer) noexcept : m_Pointer(pointer)
{
    if (m_Pointer != nullptr)
    {
        m_Pointer->AddRef();
    }
}

template<typename T>
IntrusivePtr<T>::IntrusivePtr(T* pointer, AdoptRefTag) noexcept : m_Pointer(pointer)
{
}

template<typename T>
IntrusivePtr<T>::~IntrusivePtr() noexcept
{
    reset();
}

template<typename T>
IntrusivePtr<T>::IntrusivePtr(const IntrusivePtr& other) noexcept
    : IntrusivePtr(other.m_Pointer)
{
}

template<typename T>
IntrusivePtr<T>& IntrusivePtr<T>::operator=(const IntrusivePtr& other) noexcept
{
    if (this != &other)
    {
        IntrusivePtr temporary(other);
        swap(temporary);
    }
    return *this;
}

template<typename T>
IntrusivePtr<T>::IntrusivePtr(IntrusivePtr&& other) noexcept
    : m_Pointer(other.m_Pointer)
{
    other.m_Pointer = nullptr;
}

template<typename T>
IntrusivePtr<T>& IntrusivePtr<T>::operator=(IntrusivePtr&& other) noexcept
{
    if (this != &other)
    {
        reset();
        m_Pointer = other.m_Pointer;
        other.m_Pointer = nullptr;
    }
    return *this;
}

template<typename T>
T* IntrusivePtr<T>::get() const noexcept
{
    return m_Pointer;
}

template<typename T>
IntrusivePtr<T>::operator bool() const noexcept
{
    return m_Pointer != nullptr;
}

template<typename T>
T& IntrusivePtr<T>::operator*() const noexcept
{
    assert(m_Pointer != nullptr);
    return *m_Pointer;
}

template<typename T>
T* IntrusivePtr<T>::operator->() const noexcept
{
    assert(m_Pointer != nullptr);
    return m_Pointer;
}

template<typename T>
void IntrusivePtr<T>::reset() noexcept
{
    T* pointer = m_Pointer;
    m_Pointer = nullptr;
    if (pointer != nullptr)
    {
        pointer->ReleaseRef();
    }
}

template<typename T>
void IntrusivePtr<T>::swap(IntrusivePtr& other) noexcept
{
    using std::swap;
    swap(m_Pointer, other.m_Pointer);
}

template<typename T, typename... Args>
IntrusivePtr<T> MakeIntrusive(Args&&... args)
{
    static_assert(std::is_base_of_v<IntrusiveRefCounted, T>,
        "MakeIntrusive<T> requires IntrusiveRefCounted base");
    return IntrusivePtr<T>(new T(std::forward<Args>(args)...));
}

} // namespace Raven
