#pragma once

#include "Raven/UI/Document/DocumentImporterRegistry.h"
#include "Raven/UI/Document/UIDocument.h"

#include <string>

namespace Raven
{

// ファイルパスから適切なImporterを選択し、UIDocumentへ読み込む共通入口です。
// UI Runtimeは具体的なSvgImporter等を生成せず、このLoaderだけに依存できます。
class DocumentLoader
{
public:
    DocumentLoader();

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
