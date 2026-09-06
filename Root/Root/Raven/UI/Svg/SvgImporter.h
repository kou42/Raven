#pragma once

#include "Raven/UI/Document/IDocumentImporter.h"

#include <string>

namespace Raven
{

struct SvgImportContext;

// SVG固有の構文解析を担当するImporterです。
// 公開境界はUIDocumentへ統一し、Runtime側からSVG固有のParser構成を隠蔽します。
class SvgImporter final : public IDocumentImporter
{
public:
    // SVG 1.1全体ではなく、Raven UIへ安全に取り込めるVector Shape subsetを扱います。
    // SVG固有構文はImporter内部で解釈し、ファイル形式非依存のUIDocumentへ正規化します。
    // pathは専用SvgPathImporterがM/L/H/V/Q/T/C/S/A/Zをopen/closed状態を持つ複数subpathのPolylineへ変換します。
    bool ImportFile(
        const std::string& path,
        UIDocument& outDocument,
        std::string* outError = nullptr) const override;

private:
    // SVG基本Shape ParserはImporter内部Contextへ直接書き込みます。
    // Runtimeへは公開せず、Viewport/Animation/Vectorの所有関係をUIDocument側へ集約します。
    static bool ImportFile(
        const std::string& path,
        SvgImportContext& context,
        std::string* outError = nullptr);
};

} // namespace Raven
