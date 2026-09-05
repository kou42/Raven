#pragma once

#include "Raven/UI/Document/IDocumentImporter.h"
#include "Raven/UI/Document/VectorDocument.h"

#include <string>

namespace Raven
{

// SVG固有の構文解析を担当するImporterです。
// 公開境界はUIDocumentへ統一し、Runtime側からSVG固有のParser構成を隠蔽します。
class SvgImporter final : public IDocumentImporter
{
public:
    bool ImportFile(
        const std::string& path,
        UIDocument& outDocument,
        std::string* outError = nullptr) const override;

private:
    // 既存の基本Shape Parserが使用する低レベルAPIです。
    // SvgImportContextへの内部移行が完了するまでは互換入口として保持しますが、Runtimeへは公開しません。
    static bool ImportFile(
        const std::string& path,
        VectorDocument& outDocument,
        std::string* outError = nullptr);
};

} // namespace Raven
