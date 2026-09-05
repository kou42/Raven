#pragma once

#include "Raven/UI/Document/IDocumentImporter.h"
#include "Raven/UI/Document/VectorDocument.h"
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
    // 既存コードとの互換性を保つ低レベルAPIです。
    // SVGの基本ShapeをVectorDocumentへ変換し、pathはSvgPathImporterが追加します。
    static bool ImportFile(const std::string& path, VectorDocument& outDocument, std::string* outError = nullptr);

    // 新しい共通Importer境界です。
    // SVGの基本Shapeとpathをここで統合し、呼び出し側が複数Parserを意識しないようにします。
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

        outDocument.ViewportSize = imported.ViewportSize;
        outDocument.Animation = std::move(imported.Animation);
        outDocument.LoopAnimation = imported.LoopAnimation;
        outDocument.Vector = std::move(imported);
        return true;
    }
};

} // namespace Raven
