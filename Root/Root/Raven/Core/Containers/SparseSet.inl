#pragma once

namespace Raven
{

inline SparseSet::size_type SparseSet::FindPagePosition(key_type pageIndex) const noexcept
{
    size_type first = 0;
    size_type last = m_SparsePages.size();
    while (first < last)
    {
        const size_type middle = first + (last - first) / 2;
        if (m_SparsePages[middle]->pageIndex < pageIndex)
        {
            first = middle + 1;
        }
        else
        {
            last = middle;
        }
    }
    return first;
}

inline SparseSet::SparsePage* SparseSet::FindPage(key_type pageIndex) noexcept
{
    const size_type position = FindPagePosition(pageIndex);
    if (position >= m_SparsePages.size() || m_SparsePages[position]->pageIndex != pageIndex)
    {
        return nullptr;
    }
    return m_SparsePages[position].get();
}

inline const SparseSet::SparsePage* SparseSet::FindPage(key_type pageIndex) const noexcept
{
    const size_type position = FindPagePosition(pageIndex);
    if (position >= m_SparsePages.size() || m_SparsePages[position]->pageIndex != pageIndex)
    {
        return nullptr;
    }
    return m_SparsePages[position].get();
}

inline bool SparseSet::contains(key_type key) const noexcept
{
    const key_type pageIndex = static_cast<key_type>(key / SparsePageSize);
    const SparsePage* page = FindPage(pageIndex);
    if (page == nullptr)
    {
        return false;
    }
    const size_type pageOffset = static_cast<size_type>(key % SparsePageSize);
    const size_type denseIndex = page->denseIndices[pageOffset];
    return denseIndex < m_Dense.size() && m_Dense[denseIndex] == key;
}

inline bool SparseSet::insert(key_type key)
{
    if (contains(key))
    {
        return false;
    }

    const key_type pageIndex = static_cast<key_type>(key / SparsePageSize);
    SparsePage* page = FindPage(pageIndex);
    if (page == nullptr)
    {
        auto newPage = std::make_unique<SparsePage>();
        newPage->pageIndex = pageIndex;
        newPage->denseIndices.fill(InvalidIndex);

        const size_type position = FindPagePosition(pageIndex);
        // vector拡張やPage確保が失敗してもDense側へはまだ追加していないため、
        // Setの論理状態は変化しません。unique_ptrの移動はnoexceptです。
        const auto inserted = m_SparsePages.insert(
            m_SparsePages.begin() + static_cast<std::ptrdiff_t>(position), std::move(newPage));
        page = inserted->get();
    }

    // push_back失敗時には空Pageが残る場合がありますが、対応Indexは更新しないため
    // contains/sizeなどの論理状態と既存要素の対応は維持されます。
    m_Dense.push_back(key);
    page->denseIndices[static_cast<size_type>(key % SparsePageSize)] = m_Dense.size() - 1;
    return true;
}

inline bool SparseSet::erase(key_type key) noexcept
{
    if (contains(key) == false)
    {
        return false;
    }
    SparsePage* page = FindPage(static_cast<key_type>(key / SparsePageSize));
    assert(page != nullptr);
    const size_type pageOffset = static_cast<size_type>(key % SparsePageSize);
    const size_type denseIndex = page->denseIndices[pageOffset];
    const key_type lastKey = m_Dense.back();

    // 密な配列の穴を末尾要素で埋め、移動したIDの逆引きも更新します。
    m_Dense[denseIndex] = lastKey;
    SparsePage* lastPage = FindPage(static_cast<key_type>(lastKey / SparsePageSize));
    assert(lastPage != nullptr);
    lastPage->denseIndices[static_cast<size_type>(lastKey % SparsePageSize)] = denseIndex;
    m_Dense.pop_back();
    page->denseIndices[pageOffset] = InvalidIndex;
    return true;
}

inline void SparseSet::clear() noexcept
{
    // 確保済みPageを再利用できるよう保持しつつ、登録済みIDの対応だけを無効化します。
    for (key_type key : m_Dense)
    {
        SparsePage* page = FindPage(static_cast<key_type>(key / SparsePageSize));
        assert(page != nullptr);
        page->denseIndices[static_cast<size_type>(key % SparsePageSize)] = InvalidIndex;
    }
    m_Dense.clear();
}

inline void SparseSet::reserve(size_type denseCapacity, size_type sparseCapacity)
{
    m_Dense.reserve(denseCapacity);
    // sparseCapacityは従来どおり想定するID範囲の要素数として扱い、
    // ページ化後はその範囲を覆うPageポインタ数へ換算して予約します。
    const size_type pageCapacity = sparseCapacity / SparsePageSize +
        (sparseCapacity % SparsePageSize != 0 ? 1 : 0);
    m_SparsePages.reserve(pageCapacity);
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
