#pragma once

#include "Raven/UI/Document/IDocumentImporter.h"
#include "Raven/UI/Document/VectorDocument.h"

#include <string>

namespace Raven
{

// SVG固有の構文解析を担当するImporterです。
// 公開境界はUIDocumentへ統一し、Runtime側からSVG固有処理とVectorDocument互換処理を隠蔽します。
class SvgImporter final : public IDocumentImporter
{
public:
    bool ImportFile(
        const std::string& path,
        UIDocument& outDocument,
        std::string* outError = nullptr) const override;

private:
    // 基本Shapeの解析は既存実装を段階的に移行するため、当面VectorDocumentを内部作業領域として利用します。
    // このAPIは外部へ公開せず、Viewport/Animationの共通状態はSvgImportContextへ集約します。
    static bool ImportVectorShapes(
        const std::string& path,
        VectorDocument& outDocument,
        std::string* outError = nullptr);
};

} // namespace Raven
