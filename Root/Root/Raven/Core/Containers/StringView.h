#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace Raven
{

// UTF-8文字列の連続したbyte列を所有せず参照するViewです。
// size()と各indexはUnicode文字数ではなくbyte数を表し、終端nullも保証しません。
// 参照元の破棄・再確保による無効化は呼び出し側が管理してください。
class StringView
{
public:
    using size_type = std::size_t;
    using const_iterator = const char*;

    static constexpr size_type npos = static_cast<size_type>(-1);

    StringView() noexcept = default;
    StringView(const char* text) noexcept;
    StringView(const char* data, size_type size) noexcept;
    StringView(std::string_view view) noexcept;
    StringView(const std::string& text) noexcept;

    [[nodiscard]] const char* data() const noexcept;
    [[nodiscard]] size_type size() const noexcept;
    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] const_iterator begin() const noexcept;
    [[nodiscard]] const_iterator end() const noexcept;

    const char& operator[](size_type index) const noexcept;
    const char& front() const noexcept;
    const char& back() const noexcept;

    void remove_prefix(size_type count) noexcept;
    void remove_suffix(size_type count) noexcept;
    [[nodiscard]] StringView substr(size_type offset, size_type count = npos) const noexcept;

    [[nodiscard]] int compare(StringView other) const noexcept;
    [[nodiscard]] bool starts_with(StringView prefix) const noexcept;
    [[nodiscard]] bool ends_with(StringView suffix) const noexcept;
    [[nodiscard]] size_type find(char value, size_type offset = 0) const noexcept;

    [[nodiscard]] std::string_view ToStdStringView() const noexcept;

private:
    const char* m_Data = nullptr;
    size_type m_Size = 0;
};

bool operator==(StringView left, StringView right) noexcept;
bool operator!=(StringView left, StringView right) noexcept;
bool operator<(StringView left, StringView right) noexcept;

} // namespace Raven
