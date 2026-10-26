#include "Raven/Core/Containers/String.h"

#include <cassert>
#include <utility>

namespace Raven
{

String::String(Allocator& allocator)
    : m_Value(STLAllocatorAdapter<char>(allocator))
{
}

String::String(Allocator& allocator, const char* text)
    : String(allocator, StringView(text))
{
}

String::String(Allocator& allocator, const char* data, size_type size)
    : String(allocator, StringView(data, size))
{
}

String::String(Allocator& allocator, StringView view)
    : m_Value(view.data() != nullptr ? view.data() : "", view.size(), STLAllocatorAdapter<char>(allocator))
{
}

String::String(Allocator& allocator, std::string_view view)
    : String(allocator, StringView(view))
{
}

String::String(const String& other)
    : m_Value(other.m_Value, other.m_Value.get_allocator())
{
}

String& String::operator=(const String& other)
{
    if (this != &other)
    {
        assign(other.view());
    }
    return *this;
}

String& String::operator=(String&& other)
{
    if (this != &other)
    {
        // basic_stringのAllocator伝播規則により、異なるAllocator間では移動先の
        // Allocatorを維持したまま内容だけを移します。
        m_Value = std::move(other.m_Value);
    }
    return *this;
}

String::size_type String::size() const noexcept
{
    return m_Value.size();
}

String::size_type String::capacity() const noexcept
{
    return m_Value.capacity();
}

bool String::empty() const noexcept
{
    return m_Value.empty();
}

char* String::data() noexcept
{
    return m_Value.data();
}

const char* String::data() const noexcept
{
    return m_Value.data();
}

const char* String::c_str() const noexcept
{
    return m_Value.c_str();
}

char& String::operator[](size_type index) noexcept
{
    assert(index < size());
    return m_Value[index];
}

const char& String::operator[](size_type index) const noexcept
{
    assert(index < size());
    return m_Value[index];
}

char& String::front() noexcept
{
    assert(empty() == false);
    return m_Value.front();
}

const char& String::front() const noexcept
{
    assert(empty() == false);
    return m_Value.front();
}

char& String::back() noexcept
{
    assert(empty() == false);
    return m_Value.back();
}

const char& String::back() const noexcept
{
    assert(empty() == false);
    return m_Value.back();
}

String::iterator String::begin() noexcept
{
    return m_Value.data();
}

String::const_iterator String::begin() const noexcept
{
    return m_Value.data();
}

String::const_iterator String::cbegin() const noexcept
{
    return m_Value.data();
}

String::iterator String::end() noexcept
{
    return m_Value.data() + m_Value.size();
}

String::const_iterator String::end() const noexcept
{
    return m_Value.data() + m_Value.size();
}

String::const_iterator String::cend() const noexcept
{
    return m_Value.data() + m_Value.size();
}

void String::clear() noexcept
{
    m_Value.clear();
}

void String::reserve(size_type capacity)
{
    m_Value.reserve(capacity);
}

void String::resize(size_type size, char value)
{
    m_Value.resize(size, value);
}

void String::push_back(char value)
{
    m_Value.push_back(value);
}

void String::pop_back()
{
    assert(empty() == false);
    m_Value.pop_back();
}

String& String::assign(StringView view)
{
    m_Value.assign(view.data() != nullptr ? view.data() : "", view.size());
    return *this;
}

String& String::append(StringView view)
{
    m_Value.append(view.data() != nullptr ? view.data() : "", view.size());
    return *this;
}

String& String::operator+=(StringView view)
{
    return append(view);
}

String& String::operator+=(char value)
{
    push_back(value);
    return *this;
}

StringView String::view() const noexcept
{
    return StringView(m_Value.data(), m_Value.size());
}

std::string_view String::ToStdStringView() const noexcept
{
    return std::string_view(m_Value.data(), m_Value.size());
}

Allocator* String::GetAllocator() const noexcept
{
    return m_Value.get_allocator().GetAllocator();
}

bool operator==(const String& left, StringView right) noexcept
{
    return left.view() == right;
}

bool operator==(StringView left, const String& right) noexcept
{
    return left == right.view();
}

bool operator==(const String& left, const String& right) noexcept
{
    return left.view() == right.view();
}

bool operator!=(const String& left, StringView right) noexcept
{
    return (left == right) == false;
}

bool operator!=(StringView left, const String& right) noexcept
{
    return (left == right) == false;
}

bool operator!=(const String& left, const String& right) noexcept
{
    return (left == right) == false;
}

} // namespace Raven
