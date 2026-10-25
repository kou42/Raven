#pragma once

namespace Raven
{

template<std::size_t N>
constexpr typename BitSet<N>::size_type BitSet<N>::size() const noexcept
{
    return N;
}

template<std::size_t N>
constexpr bool BitSet<N>::empty() const noexcept
{
    return N == 0;
}

template<std::size_t N>
constexpr typename BitSet<N>::word_type BitSet<N>::LastWordMask() noexcept
{
    if constexpr (N % BitsPerWord == 0)
    {
        return ~word_type{0};
    }
    else
    {
        return (word_type{1} << (N % BitsPerWord)) - 1;
    }
}

template<std::size_t N>
bool BitSet<N>::test(size_type index) const
{
    if (index >= N)
    {
        throw std::out_of_range("BitSet index out of range");
    }
    return (*this)[index];
}

template<std::size_t N>
bool BitSet<N>::operator[](size_type index) const noexcept
{
    assert(index < N);
    return (m_Words[index / BitsPerWord] & (word_type{1} << (index % BitsPerWord))) != 0;
}

template<std::size_t N>
BitSet<N>& BitSet<N>::set(size_type index, bool value)
{
    if (index >= N)
    {
        throw std::out_of_range("BitSet index out of range");
    }
    const word_type mask = word_type{1} << (index % BitsPerWord);
    word_type& word = m_Words[index / BitsPerWord];
    if (value)
    {
        word |= mask;
    }
    else
    {
        word &= ~mask;
    }
    return *this;
}

template<std::size_t N>
BitSet<N>& BitSet<N>::reset(size_type index)
{
    return set(index, false);
}

template<std::size_t N>
BitSet<N>& BitSet<N>::flip(size_type index)
{
    if (index >= N)
    {
        throw std::out_of_range("BitSet index out of range");
    }
    m_Words[index / BitsPerWord] ^= word_type{1} << (index % BitsPerWord);
    return *this;
}

template<std::size_t N>
BitSet<N>& BitSet<N>::set() noexcept
{
    for (word_type& word : m_Words)
    {
        word = ~word_type{0};
    }
    if constexpr (WordCount > 0)
    {
        m_Words[WordCount - 1] &= LastWordMask();
    }
    return *this;
}

template<std::size_t N>
BitSet<N>& BitSet<N>::reset() noexcept
{
    for (word_type& word : m_Words)
    {
        word = 0;
    }
    return *this;
}

template<std::size_t N>
BitSet<N>& BitSet<N>::flip() noexcept
{
    for (word_type& word : m_Words)
    {
        word = ~word;
    }
    if constexpr (WordCount > 0)
    {
        m_Words[WordCount - 1] &= LastWordMask();
    }
    return *this;
}

template<std::size_t N>
bool BitSet<N>::any() const noexcept
{
    for (word_type word : m_Words)
    {
        if (word != 0)
        {
            return true;
        }
    }
    return false;
}

template<std::size_t N>
bool BitSet<N>::none() const noexcept
{
    return any() == false;
}

template<std::size_t N>
bool BitSet<N>::all() const noexcept
{
    if constexpr (N == 0)
    {
        return true;
    }
    else
    {
        for (size_type index = 0; index + 1 < WordCount; ++index)
        {
            if (m_Words[index] != ~word_type{0})
            {
                return false;
            }
        }
        return m_Words[WordCount - 1] == LastWordMask();
    }
}

template<std::size_t N>
typename BitSet<N>::size_type BitSet<N>::count() const noexcept
{
    size_type result = 0;
    for (word_type word : m_Words)
    {
        // Kernighan法: 立っているbitの数だけ反復します。
        while (word != 0)
        {
            word &= word - 1;
            ++result;
        }
    }
    return result;
}

} // namespace Raven
