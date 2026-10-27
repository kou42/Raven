#pragma once

#include "Raven/Core/Containers/HandlePool.h"

#include <memory>

namespace Raven
{

// Poolを所有せず、Poolの生存期間とgenerationを検査して参照します。
// 同じPoolに対する操作は呼び出し側で同期してください。
template<typename T>
class WeakHandle
{
public:
    WeakHandle() noexcept = default;

    [[nodiscard]] T* get() const noexcept;
    [[nodiscard]] bool expired() const noexcept;
    [[nodiscard]] PoolHandle handle() const noexcept;

private:
    friend class HandlePool<T>;

    WeakHandle(HandlePool<T>& pool, PoolHandle handle,
        const std::shared_ptr<void>& lifetime) noexcept;

    HandlePool<T>* m_Pool = nullptr;
    PoolHandle m_Handle{};
    std::weak_ptr<void> m_Lifetime;
};

} // namespace Raven

#include "Raven/Core/Containers/WeakHandle.inl"
