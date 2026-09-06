#pragma once

#include <string>

namespace Raven
{

struct SvgImportContext;

// SVG pathはcommand grammarが他shapeの属性解析より複雑になるため、専用Parserへ分離します。
// M/L/H/V/Q/T/C/S/A/Z をAdaptive TessellationでPolylineへ正規化し、
// 複数の閉じたsubpathをVectorDocumentへ輪郭単位で保持します。
// fill-rule等のpresentation属性はSvgPathStyleImporterで別途Vector semanticsへ正規化し、
// opacity animationは他shapeと同じSvgImportContext経由でUIDocumentのAnimationへ集約します。
// open pathのstroke描画は後続拡張です。
class SvgPathImporter
{
public:
    static bool AppendFilePaths(
        const std::string& path,
        SvgImportContext& context,
        std::string* outError = nullptr);
};

} // namespace Raven
