#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace Raven
{

// UTF-8の不正入力はU+FFFDへ置換します。入力を必ず1 byte以上進めるため、
// 壊れたテキストでも無限ループせず、次の正常な文字を読み取れます。
class UIUtf8
{
public:
    static constexpr std::uint32_t ReplacementCharacter = 0xFFFDu;

    static bool DecodeNext(std::string_view text, std::size_t& offset, std::uint32_t& codepoint)
    {
        if (offset >= text.size())
        {
            return false;
        }

        const unsigned char first = static_cast<unsigned char>(text[offset]);
        if (first < 0x80u)
        {
            codepoint = first;
            ++offset;
            return true;
        }

        std::size_t count = 0u;
        std::uint32_t value = 0u;
        std::uint32_t minimum = 0u;
        if (first >= 0xC2u && first <= 0xDFu)
        {
            count = 2u;
            value = first & 0x1Fu;
            minimum = 0x80u;
        }
        else if (first >= 0xE0u && first <= 0xEFu)
        {
            count = 3u;
            value = first & 0x0Fu;
            minimum = 0x800u;
        }
        else if (first >= 0xF0u && first <= 0xF4u)
        {
            count = 4u;
            value = first & 0x07u;
            minimum = 0x10000u;
        }
        else
        {
            codepoint = ReplacementCharacter;
            ++offset;
            return true;
        }

        if (count > text.size() - offset)
        {
            codepoint = ReplacementCharacter;
            ++offset;
            return true;
        }

        for (std::size_t index = 1u; index < count; ++index)
        {
            const unsigned char next = static_cast<unsigned char>(text[offset + index]);
            if ((next & 0xC0u) != 0x80u)
            {
                codepoint = ReplacementCharacter;
                ++offset;
                return true;
            }
            value = (value << 6u) | (next & 0x3Fu);
        }

        // Overlong、UTF-16 surrogate、Unicode最大値超過を拒否します。
        if (value < minimum || value > 0x10FFFFu ||
            (value >= 0xD800u && value <= 0xDFFFu))
        {
            codepoint = ReplacementCharacter;
            ++offset;
            return true;
        }

        offset += count;
        codepoint = value;
        return true;
    }
};

} // namespace Raven
