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

    explicit Vector(Allocator& allocator)
        : m_Values(STLAllocatorAdapter<T>(allocator))
    {
    }

    Vector(Allocator& allocator, std::initializer_list<T> values)
        : m_Values(values, STLAllocatorAdapter<T>(allocator))
    {
    }

    Vector(const Vector&) = default;
    Vector(Vector&&) = default;
    Vector& operator=(const Vector&) = delete;
    Vector& operator=(Vector&&) = delete;
    ~Vector() = default;

    [[nodiscard]] size_type size() const noexcept { return m_Values.size(); }
    [[nodiscard]] size_type capacity() const noexcept { return m_Values.capacity(); }
    [[nodiscard]] bool empty() const noexcept { return m_Values.empty(); }
    [[nodiscard]] T* data() noexcept { return m_Values.data(); }
    [[nodiscard]] const T* data() const noexcept { return m_Values.data(); }

    T& operator[](size_type index) noexcept
    {
        assert(index < size());
        return m_Values[index];
    }
    const T& operator[](size_type index) const noexcept
    {
        assert(index < size());
        return m_Values[index];
    }

    T& front() noexcept { return m_Values.front(); }
    const T& front() const noexcept { return m_Values.front(); }
    T& back() noexcept { return m_Values.back(); }
    const T& back() const noexcept { return m_Values.back(); }

    iterator begin() noexcept { return m_Values.begin(); }
    const_iterator begin() const noexcept { return m_Values.begin(); }
    const_iterator cbegin() const noexcept { return m_Values.cbegin(); }
    iterator end() noexcept { return m_Values.end(); }
    const_iterator end() const noexcept { return m_Values.end(); }
    const_iterator cend() const noexcept { return m_Values.cend(); }

    void clear() noexcept { m_Values.clear(); }
    void reserve(size_type capacity) { m_Values.reserve(capacity); }
    void resize(size_type count) { m_Values.resize(count); }
    void resize(size_type count, const T& value) { m_Values.resize(count, value); }
    void push_back(const T& value) { m_Values.push_back(value); }
    void push_back(T&& value) { m_Values.push_back(std::move(value)); }

    template<typename... Args>
    T& emplace_back(Args&&... args)
    {
        return m_Values.emplace_back(std::forward<Args>(args)...);
    }

    void pop_back()
    {
        assert(empty() == false);
        m_Values.pop_back();
    }

    [[nodiscard]] Allocator* GetAllocator() const noexcept
    {
        return m_Values.get_allocator().GetAllocator();
    }

private:
    std::vector<T, STLAllocatorAdapter<T>> m_Values;
};

} // namespace Raven
