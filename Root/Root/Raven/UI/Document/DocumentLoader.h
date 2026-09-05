#pragma once

#include "Raven/UI/Document/DocumentImporterRegistry.h"
#include "Raven/UI/Document/UIDocument.h"

#include <string>

namespace Raven
{

// ファイルパスから適切なImporterを選択し、UIDocumentへ読み込む共通入口です。
// UI Runtimeは具体的なImporterを生成せず、このLoaderとRegistryだけに依存できます。
class DocumentLoader
{
public:
    // Raven標準のImporterを登録したLoaderを構築します。
    DocumentLoader();

    // Editor/Plugin/Test等から独自Registryを注入できるようにします。
    // 標準Importerを不要とする用途でもLoader本体を再実装せず利用できます。
    explicit DocumentLoader(DocumentImporterRegistry registry);

    bool CanLoad(const std::string& path) const;

    bool Load(
        const std::string& path,
        UIDocument& outDocument,
        std::string* outError = nullptr) const;

    DocumentImporterRegistry& GetRegistry() { return m_Registry; }
    const DocumentImporterRegistry& GetRegistry() const { return m_Registry; }

private:
    static std::string GetExtension(const std::string& path);

    DocumentImporterRegistry m_Registry;
};

} // namespace Raven
