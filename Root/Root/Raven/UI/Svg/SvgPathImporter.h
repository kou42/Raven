#pragma once

#include "Raven/UI/Document/VectorDocument.h"

#include <string>

namespace Raven
{

// SVG pathはcommand grammarが他shapeの属性解析より複雑になるため、専用Parserへ分離します。
// M/L/H/V/Q/T/C/S/A/Z をAdaptive TessellationでPolylineへ正規化し、
// 複数の閉じたsubpathをVectorDocumentへ輪郭単位で保持します。fill-rule、open pathのstroke描画は後続拡張です。
class SvgPathImporter
{
public:
    static bool AppendFilePaths(
        const std::string& path,
        VectorDocument& document,
        std::string* outError = nullptr);
};

} // namespace Raven
