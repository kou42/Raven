#pragma once

#include "Raven/Core/Memory/Allocator.h"

#include <cstddef>
#include <type_traits>
#include <utility>

namespace Raven
{

// 個別解放可能なAllocatorから確保した単一オブジェクトを所有します。
// Allocatorは所有ポインタより長生きし、Reset()は所有ポインタの破棄後に行ってください。
// Frame/LinearのようにDeallocateがno-opのAllocatorは使用しないでください。
template<typename T>
class UniquePtr
{
    static_assert(std::is_nothrow_destructible_v<T>, "UniquePtr requires noexcept destructible T");

public:
    UniquePtr() noexcept = default;
    UniquePtr(std::nullptr_t) noexcept;
    UniquePtr(T* pointer, Allocator& allocator) noexcept;
    ~UniquePtr() noexcept;

    UniquePtr(const UniquePtr&) = delete;
    UniquePtr& operator=(const UniquePtr&) = delete;
    UniquePtr(UniquePtr&& other) noexcept;
    UniquePtr& operator=(UniquePtr&& other) noexcept;

    [[nodiscard]] T* get() const noexcept;
    [[nodiscard]] Allocator* GetAllocator() const noexcept;
    [[nodiscard]] explicit operator bool() const noexcept;
    T& operator*() const noexcept;
    T* operator->() const noexcept;

    void reset() noexcept;
    // release後は呼び出し側がデストラクタ実行と元AllocatorへのDeallocateを担当します。
    [[nodiscard]] T* release() noexcept;
    void swap(UniquePtr& other) noexcept;

private:
    T* m_Pointer = nullptr;
    Allocator* m_Allocator = nullptr;
};

template<typename T, typename... Args>
[[nodiscard]] UniquePtr<T> MakeUnique(Allocator& allocator, Args&&... args);

} // namespace Raven

#include "Raven/Core/Containers/UniquePtr.inl"
