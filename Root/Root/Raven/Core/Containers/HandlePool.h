#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace Raven
{

// index + generationでオブジェクトを識別するHandleです。
// 異なるPool間のHandle混用は検出しないため、Poolの所有関係は呼び出し側が管理します。
struct PoolHandle
{
    std::uint32_t index = (std::numeric_limits<std::uint32_t>::max)();
    std::uint32_t generation = 0;

    [[nodiscard]] bool operator==(const PoolHandle& other) const noexcept
    {
        return index == other.index && generation == other.generation;
    }
    [[nodiscard]] bool operator!=(const PoolHandle& other) const noexcept
    {
        return (*this == other) == false;
    }
};

// 再利用時にgenerationを進め、解放済みHandleの誤参照を防ぎます。
// Slot本体は個別確保するため、Pool拡張後も既存オブジェクトのアドレスを維持します。
template<typename T>
class HandlePool
{
    static_assert(std::is_nothrow_destructible_v<T>, "HandlePool requires noexcept destructible elements");

public:
    using size_type = std::size_t;
    using Handle = PoolHandle;

    HandlePool() = default;
    HandlePool(const HandlePool&) = delete;
    HandlePool& operator=(const HandlePool&) = delete;
    HandlePool(HandlePool&&) = delete;
    HandlePool& operator=(HandlePool&&) = delete;
    ~HandlePool() noexcept;

    template<typename... Args>
    Handle emplace(Args&&... args);

    [[nodiscard]] T* get(Handle handle) noexcept;
    [[nodiscard]] const T* get(Handle handle) const noexcept;
    [[nodiscard]] bool contains(Handle handle) const noexcept;
    bool erase(Handle handle) noexcept;
    void clear() noexcept;

    [[nodiscard]] size_type size() const noexcept;
    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] size_type capacity() const noexcept;

private:
    struct Slot
    {
        using Storage = std::aligned_storage_t<sizeof(T), alignof(T)>;
        Storage storage;
        std::uint32_t generation = 1;
        bool occupied = false;
        bool retired = false;
        std::uint32_t nextFree = (std::numeric_limits<std::uint32_t>::max)();

        [[nodiscard]] T* value() noexcept;
        [[nodiscard]] const T* value() const noexcept;
    };

    static constexpr std::uint32_t InvalidIndex = (std::numeric_limits<std::uint32_t>::max)();
    std::vector<std::unique_ptr<Slot>> m_Slots;
    std::uint32_t m_FreeHead = InvalidIndex;
    size_type m_Size = 0;
};

} // namespace Raven

#include "Raven/Core/Containers/HandlePool.inl"
