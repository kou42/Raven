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

    // Runtime側は具体的なImporterを知らず、DocumentLoaderから正規化済みUIDocumentだけを受け取ります。
    return SetDocument(std::move(imported), outError);
}

} // namespace Raven
