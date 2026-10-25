#pragma once

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <vector>

namespace Raven
{

// uint32_t IDを密な配列とページ化した疎配列で管理するSparse Setです。
// contains/insert/eraseはSparse Page数に対してO(log N)、反復は密なID配列を走査します。
// 削除は末尾要素とのswapで行うため、順序およびIteratorは維持されません。
// IDの再利用時に世代を検証する機能はありません（HandlePool側で管理します）。
class SparseSet
{
public:
    using key_type = std::uint32_t;
    using size_type = std::size_t;
    using const_iterator = std::vector<key_type>::const_iterator;

    [[nodiscard]] bool insert(key_type key);
    [[nodiscard]] bool erase(key_type key) noexcept;
    [[nodiscard]] bool contains(key_type key) const noexcept;
    void clear() noexcept;
    void reserve(size_type denseCapacity, size_type sparseCapacity = 0);

    [[nodiscard]] size_type size() const noexcept;
    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] const_iterator begin() const noexcept;
    [[nodiscard]] const_iterator end() const noexcept;
    [[nodiscard]] key_type operator[](size_type index) const noexcept;

private:
    static constexpr size_type InvalidIndex = (std::numeric_limits<size_type>::max)();
    static constexpr size_type SparsePageSize = 256;

    struct SparsePage
    {
        key_type pageIndex = 0;
        std::array<size_type, SparsePageSize> denseIndices{};
    };

    [[nodiscard]] size_type FindPagePosition(key_type pageIndex) const noexcept;
    [[nodiscard]] SparsePage* FindPage(key_type pageIndex) noexcept;
    [[nodiscard]] const SparsePage* FindPage(key_type pageIndex) const noexcept;

    std::vector<key_type> m_Dense;
    // ID値そのものを配列長にしません。離れた大きなIDでも必要なPageだけを確保し、
    // 単一IDの登録で数GiB級の連続確保が発生することを防ぎます。
    std::vector<std::unique_ptr<SparsePage>> m_SparsePages;
};

} // namespace Raven

#include "Raven/Core/Containers/SparseSet.inl"
