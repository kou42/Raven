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

    // 旧Parserの共通状態を読む処理はContext内部へ閉じ込めます。
    // Parser側の移行が完了すればTakeLegacyVectorDocumentだけを削除でき、Importer公開境界は変更不要です。
    SvgImportContext context;
    context.TakeLegacyVectorDocument(std::move(imported));

    // Import途中で失敗した場合に呼び出し側の既存Documentを壊さないよう、成功後にまとめて置き換えます。
    outDocument = std::move(context.Document);
    return true;
}

} // namespace Raven
