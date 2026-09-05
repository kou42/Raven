#pragma once

#include "Raven/UI/Document/UIVectorDocument.h"

#include <string>

namespace Raven
{

// SVG固有の責務はファイル解析だけに限定し、描画・Animation再生はUIVectorDocumentへ委譲します。
// これにより他形式のImporterもVectorDocumentへ正規化すれば同じRuntimeを再利用できます。
class UISvg final : public UIVectorDocument
{
public:
    bool LoadFromFile(const std::string& path, std::string* outError = nullptr);
};

} // namespace Raven
