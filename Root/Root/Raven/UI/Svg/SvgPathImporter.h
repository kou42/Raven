#pragma once

#include <string>

namespace Raven
{

struct SvgImportContext;

// SVG pathはcommand grammarが他shapeの属性解析より複雑になるため、専用Parserへ分離します。
// M/L/H/V/Q/T/C/S/A/Z をAdaptive TessellationでPolylineへ正規化し、
// open/closedを含む複数subpathとfill/stroke情報をVectorDocumentへ保持します。
// PathのAnimationも内部Contextへ集約し、基本Shapeと共通の再生設定を使用します。
// linecap/linejoinやfill-ruleは後続拡張としてRuntime側の描画規則へ分離します。
class SvgPathImporter
{
public:
    static bool AppendFilePaths(
        const std::string& path,
        SvgImportContext& context,
        std::string* outError = nullptr);
};

} // namespace Raven
