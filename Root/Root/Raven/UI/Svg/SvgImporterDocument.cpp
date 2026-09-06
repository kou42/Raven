#include "Raven/UI/Svg/SvgImporter.h"

#include "Raven/UI/Svg/SvgImportContext.h"
#include "Raven/UI/Svg/SvgPathImporter.h"
#include "Raven/UI/Svg/SvgPathStyleImporter.h"

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

    // Path geometryは専用ParserでPolylineへ正規化し、presentation属性は後段Style Importerで
    // Vector semanticsへ変換します。geometry grammarとstyle解釈を分離して責務を明確に保ちます。
    if (SvgPathImporter::AppendFilePaths(path, context.GetVectorDocument(), outError) == false)
    {
        return false;
    }
    if (SvgPathStyleImporter::ApplyFilePathStyles(
            path,
            context.GetVectorDocument(),
            outError) == false)
    {
        return false;
    }

    // Import途中で失敗した場合に呼び出し側の既存Documentを壊さないよう、成功後にまとめて置き換えます。
    outDocument = std::move(context.Document);
    return true;
}

} // namespace Raven
