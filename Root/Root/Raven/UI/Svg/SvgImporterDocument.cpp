#include "Raven/UI/Svg/SvgImporter.h"

#include "Raven/UI/Svg/SvgImportContext.h"
#include "Raven/UI/Svg/SvgPathImporter.h"

#include <utility>

namespace Raven
{

bool SvgImporter::ImportFile(
    const std::string& path,
    UIDocument& outDocument,
    std::string* outError) const
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

    // 基本Shape ParserがまだVectorDocumentへ保持している共通状態を、
    // Runtimeが参照するUIDocumentへ一度だけ正規化します。
    // 次段階で基本Shape ParserをSvgImportContextへ直接移行すれば、この互換コピー自体を削除できます。
    SvgImportContext context;
    context.Document.ViewportSize = imported.ViewportSize;
    context.Document.Animation = std::move(imported.Animation);
    context.Document.LoopAnimation = imported.LoopAnimation;
    context.Document.Vector = std::move(imported);

    // Import途中で失敗した場合に呼び出し側の既存Documentを壊さないよう、成功後にまとめて置き換えます。
    outDocument = std::move(context.Document);
    return true;
}

} // namespace Raven
