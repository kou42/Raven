#pragma once

#include "Raven/UI/Document/IDocumentImporter.h"
#include "Raven/UI/Document/VectorDocument.h"
#include "Raven/UI/Svg/SvgImportContext.h"
#include "Raven/UI/Svg/SvgPathImporter.h"

#include <string>
#include <utility>

namespace Raven
{

// SVG固有の構文解析を担当するImporterです。
// IDocumentImporter経由ではUIDocumentへ正規化し、Runtime側からSVG固有処理を隠蔽します。
class SvgImporter final : public IDocumentImporter
{
public:
    // 既存の基本Shape Parserが使用する低レベルAPIです。
    // SvgImportContextへの内部移行が完了するまでは互換入口として保持し、Runtimeからは利用しません。
    static bool ImportFile(const std::string& path, VectorDocument& outDocument, std::string* outError = nullptr);

    bool ImportFile(const std::string& path, UIDocument& outDocument, std::string* outError = nullptr) const override
    {
        VectorDocument imported;
        if (ImportFile(path, imported, outError) == false)
        {
            return false;
        }
        if (SvgPathImporter::AppendFilePaths(path, imported, outError) == false)
        {
            return false;
        }

        // 共通状態の移送はSvgImportContextへ集約します。
        // 次段階では基本Shape Parser自体がこのContextへ直接書き込み、VectorDocumentの互換フィールドを削除します。
        SvgImportContext context;
        context.Document.ViewportSize = imported.ViewportSize;
        context.Document.Animation = std::move(imported.Animation);
        context.Document.LoopAnimation = imported.LoopAnimation;
        context.Document.Vector = std::move(imported);
        outDocument = std::move(context.Document);
        return true;
    }
};

} // namespace Raven
