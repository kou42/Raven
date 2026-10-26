#pragma once

#include <cassert>
#include <new>
#include <stdexcept>

namespace Raven
{

template<typename T>
UniquePtr<T>::UniquePtr(std::nullptr_t) noexcept
{
}

template<typename T>
UniquePtr<T>::UniquePtr(T* pointer, Allocator& allocator) noexcept
    : m_Pointer(pointer),
      m_Allocator(pointer != nullptr ? &allocator : nullptr)
{
}

template<typename T>
UniquePtr<T>::~UniquePtr() noexcept
{
    reset();
}

template<typename T>
UniquePtr<T>::UniquePtr(UniquePtr&& other) noexcept
    : m_Pointer(other.m_Pointer),
      m_Allocator(other.m_Allocator)
{
    other.m_Pointer = nullptr;
    other.m_Allocator = nullptr;
}

template<typename T>
UniquePtr<T>& UniquePtr<T>::operator=(UniquePtr&& other) noexcept
{
    if (this != &other)
    {
        reset();

        m_Pointer = other.m_Pointer;
        m_Allocator = other.m_Allocator;

        other.m_Pointer = nullptr;
        other.m_Allocator = nullptr;
    }

    return *this;
}

template<typename T>
T* UniquePtr<T>::get() const noexcept
{
    return m_Pointer;
}

template<typename T>
Allocator* UniquePtr<T>::GetAllocator() const noexcept
{
    return m_Allocator;
}

template<typename T>
UniquePtr<T>::operator bool() const noexcept
{
    return m_Pointer != nullptr;
}

template<typename T>
T& UniquePtr<T>::operator*() const noexcept
{
    assert(m_Pointer != nullptr);
    return *m_Pointer;
}

template<typename T>
T* UniquePtr<T>::operator->() const noexcept
{
    assert(m_Pointer != nullptr);
    return m_Pointer;
}

template<typename T>
void UniquePtr<T>::reset() noexcept
{
    T* pointer = m_Pointer;
    Allocator* allocator = m_Allocator;

    m_Pointer = nullptr;
    m_Allocator = nullptr;

    if (pointer != nullptr)
    {
        assert(allocator != nullptr);

        // オブジェクトを破棄してから、そのメモリ領域をAllocatorへ返します。
        pointer->~T();
        allocator->Deallocate(pointer);
    }
}

template<typename T>
T* UniquePtr<T>::release() noexcept
{
    T* pointer = m_Pointer;

    m_Pointer = nullptr;
    m_Allocator = nullptr;

    return pointer;
}

template<typename T>
void UniquePtr<T>::swap(UniquePtr& other) noexcept
{
    using std::swap;

    swap(m_Pointer, other.m_Pointer);
    swap(m_Allocator, other.m_Allocator);
}

template<typename T, typename... Args>
UniquePtr<T> MakeUnique(Allocator& allocator, Args&&... args)
{
    static_assert(std::is_nothrow_destructible_v<T>, "T must be nothrow destructible");

    void* memory = allocator.Allocate(sizeof(T), alignof(T));

    if (memory == nullptr)
    {
        throw std::bad_alloc();
    }

    try
    {
        return UniquePtr<T>(
            new (memory) T(std::forward<Args>(args)...),
            allocator);
    }
    catch (...)
    {
        // コンストラクタが失敗した場合、未構築の領域のみ解放します。
        allocator.Deallocate(memory);
        throw;
    }
}

} // namespace Raven
