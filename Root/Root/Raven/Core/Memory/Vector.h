#pragma once

#include "Raven/Core/Memory/STLAllocatorAdapter.h"

#include <cassert>
#include <cstddef>
#include <initializer_list>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace Raven
{

// Phase 3では既存のSTLAllocatorAdapterを活用し、Allocator対応と
// std::vectorの例外安全性・Iterator互換性を維持します。
// 独自Storageへの移行時もPublic APIを維持できるよう、実装型は公開しません。
// 注意: AllocatorはVectorより長生きし、異なるAllocator間の代入は行わないでください。
template<typename T>
class Vector
{
public:
    using value_type = T;
    using size_type = std::size_t;
    using iterator = typename std::vector<T, STLAllocatorAdapter<T>>::iterator;
    using const_iterator = typename std::vector<T, STLAllocatorAdapter<T>>::const_iterator;

    explicit Vector(Allocator& allocator);

    Vector(Allocator& allocator, std::initializer_list<T> values);

    Vector(const Vector&) = default;
    Vector(Vector&&) = default;
    Vector& operator=(const Vector&) = delete;
    Vector& operator=(Vector&&) = delete;
    ~Vector() = default;

    [[nodiscard]] size_type size() const noexcept;
    [[nodiscard]] size_type capacity() const noexcept;
    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] T* data() noexcept;
    [[nodiscard]] const T* data() const noexcept;

    T& operator[](size_type index) noexcept;
    const T& operator[](size_type index) const noexcept;

    T& front() noexcept;
    const T& front() const noexcept;
    T& back() noexcept;
    const T& back() const noexcept;

    iterator begin() noexcept;
    const_iterator begin() const noexcept;
    const_iterator cbegin() const noexcept;
    iterator end() noexcept;
    const_iterator end() const noexcept;
    const_iterator cend() const noexcept;

    void clear() noexcept;
    void reserve(size_type capacity);
    void resize(size_type count);
    void resize(size_type count, const T& value);
    void push_back(const T& value);
    void push_back(T&& value);

    template<typename... Args>
    T& emplace_back(Args&&... args);

    void pop_back();

    [[nodiscard]] Allocator* GetAllocator() const noexcept;

private:
    std::vector<T, STLAllocatorAdapter<T>> m_Values;
};

} // namespace Raven

#include "Raven/Core/Memory/Vector.inl"
