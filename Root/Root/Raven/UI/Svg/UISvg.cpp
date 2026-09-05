#include "Raven/UI/Svg/UISvg.h"

#include "Raven/UI/Document/UIDocument.h"
#include "Raven/UI/Svg/SvgImporter.h"

#include <utility>

namespace Raven
{

bool UISvg::LoadFromFile(const std::string& path, std::string* outError)
{
    SvgImporter importer;
    UIDocument imported;
    if (importer.ImportFile(path, imported, outError) == false)
    {
        return false;
    }

    // SVG固有の解析はImporter側で完結し、Runtimeには正規化済みUIDocumentだけを渡します。
    return SetDocument(std::move(imported), outError);
}

} // namespace Raven
