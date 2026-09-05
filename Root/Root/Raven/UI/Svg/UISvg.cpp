#include "Raven/UI/Svg/UISvg.h"

#include "Raven/UI/Svg/SvgImporter.h"
#include "Raven/UI/Svg/SvgPathImporter.h"

#include <utility>

namespace Raven
{

bool UISvg::LoadFromFile(const std::string& path, std::string* outError)
{
    VectorDocument imported;
    if (SvgImporter::ImportFile(path, imported, outError) == false)
    {
        return false;
    }

    // SVG path command grammarは専用Parserで共通PathElementへ正規化します。
    // Runtime側はSVG構文を知らず、完成したVectorDocumentだけを受け取ります。
    if (SvgPathImporter::AppendFilePaths(path, imported, outError) == false)
    {
        return false;
    }

    return SetDocument(std::move(imported), outError);
}

} // namespace Raven
