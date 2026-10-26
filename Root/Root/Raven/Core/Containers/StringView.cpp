#include "Raven/Core/Containers/StringView.h"

#include <algorithm>
#include <cassert>
#include <cstring>

namespace Raven
{

StringView::StringView(const char* text) noexcept
    : m_Data(text), m_Size(text != nullptr ? std::char_traits<char>::length(text) : 0)
{
}

StringView::StringView(const char* data, size_type size) noexcept
    : m_Data(data), m_Size(size)
{
    assert(data != nullptr || size == 0);
}

StringView::StringView(std::string_view view) noexcept
    : StringView(view.data(), view.size())
{
}

StringView::StringView(const std::string& text) noexcept
    : StringView(text.data(), text.size())
{
}

const char* StringView::data() const noexcept
{
    return m_Data;
}

StringView::size_type StringView::size() const noexcept
{
    return m_Size;
}

bool StringView::empty() const noexcept
{
    return m_Size == 0;
}

StringView::const_iterator StringView::begin() const noexcept
{
    return m_Data;
}

StringView::const_iterator StringView::end() const noexcept
{
    // 空Viewでnullポインタに加算しないようにします。
    return m_Size == 0 ? m_Data : m_Data + m_Size;
}

const char& StringView::operator[](size_type index) const noexcept
{
    assert(index < m_Size);
    return m_Data[index];
}

const char& StringView::front() const noexcept
{
    assert(empty() == false);
    return m_Data[0];
}

const char& StringView::back() const noexcept
{
    assert(empty() == false);
    return m_Data[m_Size - 1];
}

void StringView::remove_prefix(size_type count) noexcept
{
    assert(count <= m_Size);
    if (count != 0)
    {
        m_Data += count;
        m_Size -= count;
    }
}

void StringView::remove_suffix(size_type count) noexcept
{
    assert(count <= m_Size);
    m_Size -= count;
}

StringView StringView::substr(size_type offset, size_type count) const noexcept
{
    assert(offset <= m_Size);
    const size_type remaining = m_Size - offset;
    const size_type resultSize = count < remaining ? count : remaining;
    return StringView(offset == 0 ? m_Data : m_Data + offset, resultSize);
}

int StringView::compare(StringView other) const noexcept
{
    const size_type commonSize = (std::min)(m_Size, other.m_Size);
    const int commonResult = commonSize == 0
        ? 0
        : std::char_traits<char>::compare(m_Data, other.m_Data, commonSize);
    if (commonResult != 0)
    {
        return commonResult;
    }
    if (m_Size < other.m_Size)
    {
        return -1;
    }
    if (m_Size > other.m_Size)
    {
        return 1;
    }
    return 0;
}

bool StringView::starts_with(StringView prefix) const noexcept
{
    return prefix.m_Size <= m_Size && substr(0, prefix.m_Size) == prefix;
}

bool StringView::ends_with(StringView suffix) const noexcept
{
    return suffix.m_Size <= m_Size && substr(m_Size - suffix.m_Size) == suffix;
}

StringView::size_type StringView::find(char value, size_type offset) const noexcept
{
    if (offset >= m_Size)
    {
        return npos;
    }

    const void* result = std::memchr(m_Data + offset, static_cast<unsigned char>(value), m_Size - offset);
    if (result == nullptr)
    {
        return npos;
    }
    return static_cast<const char*>(result) - m_Data;
}

std::string_view StringView::ToStdStringView() const noexcept
{
    return std::string_view(m_Data != nullptr ? m_Data : "", m_Size);
}

bool operator==(StringView left, StringView right) noexcept
{
    return left.compare(right) == 0;
}

bool operator!=(StringView left, StringView right) noexcept
{
    return (left == right) == false;
}

bool operator<(StringView left, StringView right) noexcept
{
    return left.compare(right) < 0;
}

} // namespace Raven
