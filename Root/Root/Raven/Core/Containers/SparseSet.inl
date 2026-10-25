#pragma once

namespace Raven
{

inline bool SparseSet::contains(key_type key) const noexcept
{
    const size_type sparseIndex = static_cast<size_type>(key);
    if (sparseIndex >= m_Sparse.size())
    {
        return false;
    }
    const size_type denseIndex = m_Sparse[sparseIndex];
    return denseIndex < m_Dense.size() && m_Dense[denseIndex] == key;
}

inline bool SparseSet::insert(key_type key)
{
    if (contains(key))
    {
        return false;
    }

    const size_type sparseIndex = static_cast<size_type>(key);
    if (sparseIndex >= m_Sparse.size())
    {
        // resizeが失敗した場合、既存のSetの内容は変更されません。
        // 巨大IDは巨大なSparse配列を要求するため、呼び出し側でID上限を管理してください。
        m_Sparse.resize(sparseIndex + 1, InvalidIndex);
    }

    // push_back失敗時にSparse側の対応を更新しないことで整合性を維持します。
    m_Dense.push_back(key);
    m_Sparse[sparseIndex] = m_Dense.size() - 1;
    return true;
}

inline bool SparseSet::erase(key_type key) noexcept
{
    if (contains(key) == false)
    {
        return false;
    }
    const size_type sparseIndex = static_cast<size_type>(key);
    const size_type denseIndex = m_Sparse[sparseIndex];
    const key_type lastKey = m_Dense.back();

    // 密な配列の穴を末尾要素で埋め、移動したIDの逆引きも更新します。
    m_Dense[denseIndex] = lastKey;
    m_Sparse[static_cast<size_type>(lastKey)] = denseIndex;
    m_Dense.pop_back();
    m_Sparse[sparseIndex] = InvalidIndex;
    return true;
}

inline void SparseSet::clear() noexcept
{
    // 以前のSparse配列を保持しつつ、登録済みIDの対応だけを無効化します。
    for (key_type key : m_Dense)
    {
        m_Sparse[static_cast<size_type>(key)] = InvalidIndex;
    }
    m_Dense.clear();
}

inline void SparseSet::reserve(size_type denseCapacity, size_type sparseCapacity)
{
    m_Dense.reserve(denseCapacity);
    m_Sparse.reserve(sparseCapacity);
}

inline SparseSet::size_type SparseSet::size() const noexcept
{
    return m_Dense.size();
}

inline bool SparseSet::empty() const noexcept
{
    return m_Dense.empty();
}

inline SparseSet::const_iterator SparseSet::begin() const noexcept
{
    return m_Dense.begin();
}

inline SparseSet::const_iterator SparseSet::end() const noexcept
{
    return m_Dense.end();
}

inline SparseSet::key_type SparseSet::operator[](size_type index) const noexcept
{
    assert(index < m_Dense.size());
    return m_Dense[index];
}

} // namespace Raven
