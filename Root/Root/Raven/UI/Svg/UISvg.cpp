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

    // UIVectorDocumentのUIDocument対応が完了するまでの移行ブリッジです。
    // SVG固有Parserの統合はImporter側で完結しているため、Runtime側はSVG path grammarを意識しません。
    imported.Vector.ViewportSize = imported.ViewportSize;
    imported.Vector.Animation = std::move(imported.Animation);
    imported.Vector.LoopAnimation = imported.LoopAnimation;
    return SetDocument(std::move(imported.Vector), outError);
}

} // namespace Raven
