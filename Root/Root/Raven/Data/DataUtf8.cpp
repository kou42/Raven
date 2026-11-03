#include "Raven/Data/DataUtf8.h"
#include <cstdint>

namespace Raven
{
namespace Data
{
namespace Detail
{
bool IsUtf8(std::string_view text)
{
    std::size_t i = 0;
    while (i < text.size())
    {
        const auto lead = static_cast<unsigned char>(text[i++]);
        if (lead < 0x80u)
        {
            continue;
        }
        std::uint32_t code = 0;
        std::uint32_t minimum = 0;
        std::size_t count = 0;
        if (lead >= 0xC2u && lead <= 0xDFu)
        {
            code = lead & 0x1Fu; minimum = 0x80u; count = 1;
        }
        else if (lead >= 0xE0u && lead <= 0xEFu)
        {
            code = lead & 0x0Fu; minimum = 0x800u; count = 2;
        }
        else if (lead >= 0xF0u && lead <= 0xF4u)
        {
            code = lead & 0x07u; minimum = 0x10000u; count = 3;
        }
        else
        {
            return false;
        }
        if (count > text.size() - i)
        {
            return false;
        }
        while (count-- > 0)
        {
            const auto byte = static_cast<unsigned char>(text[i++]);
            if ((byte & 0xC0u) != 0x80u)
            {
                return false;
            }
            code = (code << 6u) | (byte & 0x3Fu);
        }
        if (code < minimum || code > 0x10FFFFu || (code >= 0xD800u && code <= 0xDFFFu))
        {
            return false;
        }
    }
    return true;
}

}
}
}
