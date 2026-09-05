#pragma once

#include "Raven/UI/Document/VectorDocument.h"

#include <string>

namespace Raven
{

// SVG pathはcommand grammarが他shapeの属性解析より複雑になるため、専用Parserへ分離します。
// M/L/H/V/Q/T/C/S/Z BezierをAdaptive TessellationでPolylineへ正規化し、VectorDocumentへ追加します。
// A、複数subpath、open pathのstroke描画は後続拡張です。
class SvgPathImporter
{
public:
    static bool AppendFilePaths(
        const std::string& path,
        VectorDocument& document,
        std::string* outError = nullptr);
};

} // namespace Raven
