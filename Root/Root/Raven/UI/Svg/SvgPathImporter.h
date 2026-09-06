#pragma once

#include "Raven/UI/Document/VectorDocument.h"

#include <string>

namespace Raven
{

// SVG pathはcommand grammarが他shapeの属性解析より複雑になるため、専用Parserへ分離します。
// M/L/H/V/Q/T/C/S/A/Z をAdaptive TessellationでPolylineへ正規化し、
// open/closedを含む複数subpathとfill/stroke情報をVectorDocumentへ保持します。
// linecap/linejoinやfill-ruleは後続拡張としてRuntime側の描画規則へ分離します。
class SvgPathImporter
{
public:
    static bool AppendFilePaths(
        const std::string& path,
        VectorDocument& document,
        std::string* outError = nullptr);
};

} // namespace Raven
