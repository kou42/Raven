#pragma once

#include "Raven/Core/Containers/StringView.h"
#include "Raven/Core/Memory/STLAllocatorAdapter.h"

#include <cstddef>
#include <string>
#include <string_view>

namespace Raven
{

// Raven Allocatorから永続Storageを確保するUTF-8所有文字列です。
// 現段階ではstd::basic_stringの成熟した例外安全性とSSOを利用し、Public APIを
// Raven型へ固定します。size()はUnicode文字数ではなくUTF-8のbyte数です。
// AllocatorはStringより長生きし、個別Deallocateをサポートする必要があります。
class String
{
public:
    using size_type = std::size_t;
    using iterator = char*;
    using const_iterator = const char*;

    static constexpr size_type npos = StringView::npos;

    explicit String(Allocator& allocator);
    String(Allocator& allocator, const char* text);
    String(Allocator& allocator, const char* data, size_type size);
    String(Allocator& allocator, StringView view);
    String(Allocator& allocator, std::string_view view);

    String(const String& other);
    String(String&& other) noexcept = default;
    String& operator=(const String& other);
    String& operator=(String&& other);
    ~String() = default;

    [[nodiscard]] size_type size() const noexcept;
    [[nodiscard]] size_type capacity() const noexcept;
    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] char* data() noexcept;
    [[nodiscard]] const char* data() const noexcept;
    [[nodiscard]] const char* c_str() const noexcept;

    char& operator[](size_type index) noexcept;
    const char& operator[](size_type index) const noexcept;
    char& front() noexcept;
    const char& front() const noexcept;
    char& back() noexcept;
    const char& back() const noexcept;

    iterator begin() noexcept;
    const_iterator begin() const noexcept;
    const_iterator cbegin() const noexcept;
    iterator end() noexcept;
    const_iterator end() const noexcept;
    const_iterator cend() const noexcept;

    void clear() noexcept;
    void reserve(size_type capacity);
    void resize(size_type size, char value = '\0');
    void push_back(char value);
    void pop_back();

    String& assign(StringView view);
    String& append(StringView view);
    String& operator+=(StringView view);
    String& operator+=(char value);

    [[nodiscard]] StringView view() const noexcept;
    [[nodiscard]] std::string_view ToStdStringView() const noexcept;
    [[nodiscard]] Allocator* GetAllocator() const noexcept;

private:
    using Storage = std::basic_string<char, std::char_traits<char>, STLAllocatorAdapter<char>>;
    Storage m_Value;
};

bool operator==(const String& left, StringView right) noexcept;
bool operator==(StringView left, const String& right) noexcept;
bool operator==(const String& left, const String& right) noexcept;
bool operator!=(const String& left, StringView right) noexcept;
bool operator!=(StringView left, const String& right) noexcept;
bool operator!=(const String& left, const String& right) noexcept;

} // namespace Raven
