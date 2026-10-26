#pragma once

namespace Raven
{

template<typename T>
WeakHandle<T> HandlePool<T>::weak(Handle handle) noexcept
{
    return WeakHandle<T>(*this, handle, m_Lifetime);
}


template<typename T>
WeakHandle<T>::WeakHandle(HandlePool<T>& pool, PoolHandle handle,
    const std::shared_ptr<void>& lifetime) noexcept
    : m_Pool(&pool),
      m_Handle(handle),
      m_Lifetime(lifetime)
{
}

template<typename T>
T* WeakHandle<T>::get() const noexcept
{
    // Pool破棄後にm_Poolを逆参照しないため、生存Tokenを先に確認します。
    if (m_Lifetime.expired() || m_Pool == nullptr)
    {
        return nullptr;
    }

    return m_Pool->get(m_Handle);
}

template<typename T>
bool WeakHandle<T>::expired() const noexcept
{
    return get() == nullptr;
}

template<typename T>
PoolHandle WeakHandle<T>::handle() const noexcept
{
    return m_Handle;
}

} // namespace Raven
