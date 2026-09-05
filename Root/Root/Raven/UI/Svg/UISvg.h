#pragma once

#include "Raven/UI/Document/UIVectorDocument.h"

#include <string>

namespace Raven
{

// SVG固有の責務はファイル解析だけに限定し、描画・Animation再生はUIVectorDocumentへ委譲します。
// SvgImporterはUIDocumentへ正規化するため、Runtime側はSVG固有構文やParser構成を意識しません。
class UISvg final : public UIVectorDocument
{
public:
    bool LoadFromFile(const std::string& path, std::string* outError = nullptr);
};

} // namespace Raven
