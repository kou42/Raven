#pragma once

#include "Raven/UI/Document/UIVectorDocument.h"

#include <string>

namespace Raven
{

// SVGファイルを共通DocumentLoader経由でUIDocumentへ読み込み、Vector UIとして表示する互換Widgetです。
// Importer選択はDocumentLoaderへ委譲するため、この型はSvgImporterの具体実装へ依存しません。
class UISvg final : public UIVectorDocument
{
public:
    bool LoadFromFile(const std::string& path, std::string* outError = nullptr);
};

} // namespace Raven
