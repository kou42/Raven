#pragma once

#include "Raven/UI/Document/IDocumentImporter.h"

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

namespace Raven
{

// 拡張子とImporter生成処理の対応を管理します。
// DocumentLoader自身へ各ファイル形式の分岐を増やさず、新しいImporterを登録だけで追加できるようにします。
class DocumentImporterRegistry
{
public:
    using ImporterFactory = std::function<std::unique_ptr<IDocumentImporter>()>;

    bool Register(const std::string& extension, ImporterFactory factory);
    bool Contains(const std::string& extension) const;
    std::unique_ptr<IDocumentImporter> Create(const std::string& extension) const;

private:
    static std::string NormalizeExtension(const std::string& extension);

    std::unordered_map<std::string, ImporterFactory> m_Factories;
};

} // namespace Raven
