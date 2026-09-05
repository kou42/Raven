#pragma once

#include "Raven/UI/Svg/SvgDocument.h"

#include <string>

namespace Raven
{

// SVG pathはcommand grammarが他shapeの属性解析より複雑になるため、専用Parserへ分離します。
// M/L/H/V/ZとQ/T/C/S Bezier、A Elliptical ArcをAdaptive TessellationでPolylineへ正規化し、SvgDocumentへ追加します。
// 複数subpath、fill-rule、open pathのstroke描画は後続拡張です。
class SvgPathImporter
{
public:
    static bool AppendFilePaths(
        const std::string& path,
        SvgDocument& document,
        std::string* outError = nullptr);
};

} // namespace Raven
