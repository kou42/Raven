#pragma once

#include <array>
#include <cstdint>

namespace Raven::ph::detail
{

// ============================================================================
// Physics Debug Overlay 5x7 Font
// ============================================================================
// Debug Overlay専用の極小bitmap fontです。
// 各uint8_tは横5pixelの1行を表し、7行で1文字になります。
//
// 本格的なUI/Font Systemではありません。Physicsの診断値をエンジン開発初期から
// 画面上で確認できるようにするためのbootstrap実装です。
// 将来Dear ImGuiやTextRendererを導入した場合、このファイルだけを捨てられます。
using Glyph = std::array<uint8_t, 7>;

inline Glyph GetPhysicsDebugGlyph(char c)
{
    if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');

    switch (c)
    {
    case 'A': return {14,17,17,31,17,17,17};
    case 'B': return {30,17,17,30,17,17,30};
    case 'C': return {14,17,16,16,16,17,14};
    case 'D': return {30,17,17,17,17,17,30};
    case 'E': return {31,16,16,30,16,16,31};
    case 'F': return {31,16,16,30,16,16,16};
    case 'G': return {14,17,16,23,17,17,15};
    case 'H': return {17,17,17,31,17,17,17};
    case 'I': return {14,4,4,4,4,4,14};
    case 'J': return {7,2,2,2,18,18,12};
    case 'K': return {17,18,20,24,20,18,17};
    case 'L': return {16,16,16,16,16,16,31};
    case 'M': return {17,27,21,21,17,17,17};
    case 'N': return {17,25,21,19,17,17,17};
    case 'O': return {14,17,17,17,17,17,14};
    case 'P': return {30,17,17,30,16,16,16};
    case 'Q': return {14,17,17,17,21,18,13};
    case 'R': return {30,17,17,30,20,18,17};
    case 'S': return {15,16,16,14,1,1,30};
    case 'T': return {31,4,4,4,4,4,4};
    case 'U': return {17,17,17,17,17,17,14};
    case 'V': return {17,17,17,17,17,10,4};
    case 'W': return {17,17,17,21,21,21,10};
    case 'X': return {17,17,10,4,10,17,17};
    case 'Y': return {17,17,10,4,4,4,4};
    case 'Z': return {31,1,2,4,8,16,31};
    case '0': return {14,17,19,21,25,17,14};
    case '1': return {4,12,4,4,4,4,14};
    case '2': return {14,17,1,2,4,8,31};
    case '3': return {30,1,1,14,1,1,30};
    case '4': return {2,6,10,18,31,2,2};
    case '5': return {31,16,16,30,1,1,30};
    case '6': return {14,16,16,30,17,17,14};
    case '7': return {31,1,2,4,8,8,8};
    case '8': return {14,17,17,14,17,17,14};
    case '9': return {14,17,17,15,1,1,14};
    case ':': return {0,4,4,0,4,4,0};
    case '.': return {0,0,0,0,0,4,4};
    case '-': return {0,0,0,31,0,0,0};
    case '/': return {1,2,2,4,8,8,16};
    case '[': return {14,8,8,8,8,8,14};
    case ']': return {14,2,2,2,2,2,14};
    case ' ': return {};
    default:  return {31,17,2,4,4,0,4}; // '?' fallback
    }
}

} // namespace Raven::ph::detail
