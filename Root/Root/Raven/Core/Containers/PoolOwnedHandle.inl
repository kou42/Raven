#pragma once

namespace Raven
{

template<typename T>
PoolOwnedHandle<T>::PoolOwnedHandle(
    std::shared_ptr<HandlePool<T>> pool, PoolHandle handle) noexcept
    : m_Pool(std::move(pool)),
      m_Handle(handle)
{
}

template<typename T>
PoolOwnedHandle<T>::~PoolOwnedHandle() noexcept
{
    reset();
}

template<typename T>
PoolOwnedHandle<T>::PoolOwnedHandle(PoolOwnedHandle&& other) noexcept
    : m_Pool(std::move(other.m_Pool)),
      m_Handle(other.m_Handle)
{
    other.m_Handle = {};
}

template<typename T>
PoolOwnedHandle<T>& PoolOwnedHandle<T>::operator=(PoolOwnedHandle&& other) noexcept
{
    if (this != &other)
    {
        reset();
        m_Pool = std::move(other.m_Pool);
        m_Handle = other.m_Handle;
        other.m_Handle = {};
    }
    return *this;
}

template<typename T>
T* PoolOwnedHandle<T>::get() const noexcept
{
    if (m_Pool == nullptr)
    {
        return nullptr;
    }
    return m_Pool->get(m_Handle);
}

template<typename T>
PoolHandle PoolOwnedHandle<T>::handle() const noexcept
{
    return m_Handle;
}

template<typename T>
PoolOwnedHandle<T>::operator bool() const noexcept
{
    return get() != nullptr;
}

template<typename T>
T& PoolOwnedHandle<T>::operator*() const noexcept
{
    T* pointer = get();
    assert(pointer != nullptr);
    return *pointer;
}

template<typename T>
T* PoolOwnedHandle<T>::operator->() const noexcept
{
    T* pointer = get();
    assert(pointer != nullptr);
    return pointer;
}

template<typename T>
void PoolOwnedHandle<T>::reset() noexcept
{
    if (m_Pool != nullptr)
    {
        // generation検証付きeraseなので、同じSlotの再利用先を破棄しません。
        m_Pool->erase(m_Handle);
        m_Pool.reset();
    }
    m_Handle = {};
}

template<typename T>
void PoolOwnedHandle<T>::swap(PoolOwnedHandle& other) noexcept
{
    using std::swap;
    swap(m_Pool, other.m_Pool);
    swap(m_Handle, other.m_Handle);
}

template<typename T>
template<typename... Args>
PoolOwnedHandle<T> PoolOwnedHandle<T>::Create(
    std::shared_ptr<HandlePool<T>> pool, Args&&... args)
{
    if (pool == nullptr)
    {
        throw std::invalid_argument("PoolOwnedHandle requires a valid pool");
    }

    // emplaceが例外を送出した場合、Ownerを生成せずPoolの既存状態を維持します。
    const PoolHandle handle = pool->emplace(std::forward<Args>(args)...);
    return PoolOwnedHandle(std::move(pool), handle);
}

} // namespace Raven
