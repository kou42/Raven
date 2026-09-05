#include "Raven/UI/Svg/UISvg.h"

#include "Raven/UI/Document/DocumentLoader.h"
#include "Raven/UI/Document/UIDocument.h"

#include <utility>

namespace Raven
{

bool UISvg::LoadFromFile(const std::string& path, std::string* outError)
{
    DocumentLoader loader;
    UIDocument imported;
    if (loader.Load(path, imported, outError) == false)
    {
        return false;
    }

    // SVG path command grammarは専用Parserで共通PathElementへ正規化します。
    // Path Parserの呼び出し自体はSvgImporter内部へ移動したため、Runtime側はSVG構文を知りません。
    // Runtime側は完成したUIDocumentだけを受け取り、具体的なImporter選択もDocumentLoaderへ委譲します。
    return SetDocument(std::move(imported), outError);
}

} // namespace Raven
