#pragma once

#include "Raven/UI/Svg/SvgDocument.h"

#include <string>

namespace Raven
{

// SVG pathはcommand grammarが他shapeの属性解析より複雑になるため、専用Parserへ分離します。
// M/L/H/V/ZとQ/T/C/S Bezier、A Elliptical ArcをAdaptive TessellationでPolylineへ正規化し、
// 複数subpathのopen/closed状態とfill / fill-rule / stroke / stroke-width / stroke-miterlimit直接属性を
// SvgDocumentへ保持します。stroke-linecap / stroke-linejoinはUISvg読込時にpathへ適用し、
// CSS/style属性、transform等は後続拡張です。
class SvgPathImporter
{
public:
    static bool AppendFilePaths(
        const std::string& path,
        SvgDocument& document,
        std::string* outError = nullptr);
};

} // namespace Raven
