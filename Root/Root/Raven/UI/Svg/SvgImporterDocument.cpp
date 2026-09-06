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

    // Path ParserもVector表現とAnimationの共通状態をContextへ追加します。
    // 基本Shape ParserとPath Parserの双方が成功した後にUIDocumentを公開します。
    if (SvgPathImporter::AppendFilePaths(path, context, outError) == false)
    {
        return false;
    }

    // PathのみのAnimationや基本Shapeより長いAnimationも含め、全体の再生時間を確定します。
    context.Finalize();

    // Import途中で失敗した場合に呼び出し側の既存Documentを壊さないよう、成功後にまとめて置き換えます。
    outDocument = std::move(context.Document);
    return true;
}

} // namespace Raven
