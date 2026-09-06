#pragma once

#include "Raven/UI/Document/VectorDocument.h"

#include <string>

namespace Raven
{

// SvgPathImporterがgeometry grammarをPolylineへ正規化した後に、
// path固有のpresentation属性をVector Path semanticsへ変換します。
// geometry parserとstyle parserを分離することで、fill-rule対応のために複雑なpath command parserへ
// SVG presentation/CSS解析責務を混在させない構造にしています。
class SvgPathStyleImporter
{
public:
    static bool ApplyFilePathStyles(
        const std::string& path,
        VectorDocument& document,
        std::string* outError = nullptr);
};

} // namespace Raven
