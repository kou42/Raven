#pragma once

#include "Raven/UI/Document/UIDocument.h"

#include <string>

namespace Raven
{

// ファイル形式固有Importerが実装する共通インターフェースです。
// Runtime側はSVG等の具体的な構文を知らず、正規化済みUIDocumentだけを受け取ります。
class IDocumentImporter
{
public:
    virtual ~IDocumentImporter() = default;

    virtual bool ImportFile(
        const std::string& path,
        UIDocument& outDocument,
        std::string* outError = nullptr) const = 0;
};

} // namespace Raven
