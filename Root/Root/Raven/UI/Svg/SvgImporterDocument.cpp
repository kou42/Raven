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
    SvgImportContext context;
    if (ImportFile(path, context, outError) == false)
    {
        return false;
    }

    // Path ParserはVector表現だけを追加し、Viewport/Animation等の共通状態には触れません。
    // 基本Shape ParserとPath Parserの双方が成功した後にUIDocumentを公開します。
    if (SvgPathImporter::AppendFilePaths(path, context.GetVectorDocument(), outError) == false)
    {
        return false;
    }

    // Import途中で失敗した場合に呼び出し側の既存Documentを壊さないよう、成功後にまとめて置き換えます。
    outDocument = std::move(context.Document);
    return true;
}

} // namespace Raven
