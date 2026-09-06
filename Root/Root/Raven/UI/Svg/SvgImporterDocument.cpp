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
    // Vector semanticsへ変換します。Path内のAnimationだけは他shapeと同じImportContextへ登録し、
    // UIDocumentのAnimation所有権を一箇所へ統一します。
    if (SvgPathImporter::AppendFilePaths(path, context, outError) == false)
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

    // 基本Shape Parserは自身の終了時にもFinalizeしますが、Path Animationはその後に追加されます。
    // ここで再度集約値を確定することで、Shape/Pathの最大durationとloop設定を同じClipへ反映します。
    context.Finalize();

    // Import途中で失敗した場合に呼び出し側の既存Documentを壊さないよう、成功後にまとめて置き換えます。
    outDocument = std::move(context.Document);
    return true;
}

} // namespace Raven
