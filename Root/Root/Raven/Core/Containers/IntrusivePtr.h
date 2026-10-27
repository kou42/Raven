#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace Raven
{

// 参照カウンタをオブジェクト自身に保持し、別途control blockを確保しません。
// オブジェクトは必ずnew経由で生成し、IntrusivePtrに所有させてください。
// weak参照・循環参照の解決は提供しません。
class IntrusiveRefCounted
{
public:
    void AddRef() const noexcept;
    void ReleaseRef() const noexcept;
    [[nodiscard]] std::uint32_t GetRefCount() const noexcept;

protected:
    IntrusiveRefCounted() noexcept = default;
    virtual ~IntrusiveRefCounted() = default;

private:
    IntrusiveRefCounted(const IntrusiveRefCounted&) = delete;
    IntrusiveRefCounted& operator=(const IntrusiveRefCounted&) = delete;
    mutable std::atomic<std::uint32_t> m_RefCount{0};
};

struct AdoptRefTag
{
    explicit AdoptRefTag() = default;
};
inline constexpr AdoptRefTag AdoptRef{};

// TはIntrusiveRefCountedをpublic継承する必要があります。
// 生ポインタからの構築ではAddRefし、AdoptRef指定時は既存の参照を引き継ぎます。
template<typename T>
class IntrusivePtr
{
    static_assert(std::is_base_of_v<IntrusiveRefCounted, T>,
        "IntrusivePtr<T> requires IntrusiveRefCounted base");

public:
    IntrusivePtr() noexcept = default;
    IntrusivePtr(std::nullptr_t) noexcept;
    explicit IntrusivePtr(T* pointer) noexcept;
    IntrusivePtr(T* pointer, AdoptRefTag) noexcept;
    ~IntrusivePtr() noexcept;

    IntrusivePtr(const IntrusivePtr& other) noexcept;
    IntrusivePtr& operator=(const IntrusivePtr& other) noexcept;
    IntrusivePtr(IntrusivePtr&& other) noexcept;
    IntrusivePtr& operator=(IntrusivePtr&& other) noexcept;

    [[nodiscard]] T* get() const noexcept;
    [[nodiscard]] explicit operator bool() const noexcept;
    T& operator*() const noexcept;
    T* operator->() const noexcept;
    void reset() noexcept;
    void swap(IntrusivePtr& other) noexcept;

private:
    T* m_Pointer = nullptr;
};

template<typename T, typename... Args>
[[nodiscard]] IntrusivePtr<T> MakeIntrusive(Args&&... args);

} // namespace Raven

#include "Raven/Core/Containers/IntrusivePtr.inl"
