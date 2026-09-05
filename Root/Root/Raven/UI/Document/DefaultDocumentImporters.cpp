#include "Raven/UI/Document/DefaultDocumentImporters.h"

#include "Raven/UI/Document/DocumentImporterRegistry.h"
#include "Raven/UI/Svg/SvgImporter.h"

#include <memory>

namespace Raven
{

void RegisterDefaultDocumentImporters(DocumentImporterRegistry& registry)
{
    // 標準形式の具体Importer依存はComposition Rootだけに閉じ込めます。
    // Loader本体はRegistryだけを扱うため、形式追加時にLoad処理を変更する必要がありません。
    registry.Register(".svg", []()
    {
        return std::make_unique<SvgImporter>();
    });
}

} // namespace Raven
