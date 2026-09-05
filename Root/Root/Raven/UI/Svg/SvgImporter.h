#pragma once

#include "Raven/UI/Document/VectorDocument.h"

#include <string>

namespace Raven
{

class SvgImporter
{
public:
    // SVG 1.1全体ではなく、Raven UIへ安全に取り込めるVector Shape subsetを扱います。
    // SVG固有構文はImporter内部で解釈し、ファイル形式非依存のVectorDocumentへ直接正規化します。
    // pathは専用SvgPathImporterがM/L/H/V/Q/T/C/S/ZをPolylineへ変換します。
    static bool ImportFile(const std::string& path, VectorDocument& outDocument, std::string* outError = nullptr);
};

} // namespace Raven
