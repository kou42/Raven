#pragma once

#include "Raven/Core/Containers/HandlePool.h"

#include <cassert>
#include <memory>
#include <stdexcept>
#include <utility>

namespace Raven
{

// Poolの生存期間を共有所有し、Handleの削除責任だけを一つのOwnerへ限定します。
// 同じHandleを別のOwnerへ渡さないこと。Poolへの外部erase/clearは許可しますが、
// その場合Ownerの破棄時には世代チェックにより別オブジェクトを削除しません。
// Pool操作は非thread-safeのため、並行アクセスには呼び出し側の同期が必要です。
template<typename T>
class PoolOwnedHandle
{
public:
    PoolOwnedHandle() noexcept = default;
    ~PoolOwnedHandle() noexcept;

    PoolOwnedHandle(const PoolOwnedHandle&) = delete;
    PoolOwnedHandle& operator=(const PoolOwnedHandle&) = delete;
    PoolOwnedHandle(PoolOwnedHandle&& other) noexcept;
    PoolOwnedHandle& operator=(PoolOwnedHandle&& other) noexcept;

    [[nodiscard]] T* get() const noexcept;
    [[nodiscard]] PoolHandle handle() const noexcept;
    [[nodiscard]] explicit operator bool() const noexcept;

    T& operator*() const noexcept;
    T* operator->() const noexcept;

    void reset() noexcept;
    void swap(PoolOwnedHandle& other) noexcept;

    template<typename... Args>
    [[nodiscard]] static PoolOwnedHandle Create(
        std::shared_ptr<HandlePool<T>> pool, Args&&... args);

private:
    PoolOwnedHandle(std::shared_ptr<HandlePool<T>> pool, PoolHandle handle) noexcept;

    std::shared_ptr<HandlePool<T>> m_Pool;
    PoolHandle m_Handle{};
};

} // namespace Raven

#include "Raven/Core/Containers/PoolOwnedHandle.inl"
