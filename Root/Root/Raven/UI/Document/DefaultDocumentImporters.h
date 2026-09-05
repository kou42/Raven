#pragma once

namespace Raven
{

class DocumentImporterRegistry;

// Ravenが標準で提供するDocument ImporterをRegistryへ登録します。
// Loaderと具体形式の依存を分離し、対応形式の追加箇所をこのComposition Rootへ集約します。
void RegisterDefaultDocumentImporters(DocumentImporterRegistry& registry);

} // namespace Raven
