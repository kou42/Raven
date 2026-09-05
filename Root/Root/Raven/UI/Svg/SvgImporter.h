#pragma once

#include "Raven/UI/Svg/SvgDocument.h"

#include <string>

namespace Raven
{

class SvgImporter
{
public:
    // SVG 1.1全体ではなく、Raven UIへ安全に取り込めるVector Shape subsetを扱います。
    // svg / rect / circle / ellipse / line / polygonを属性Parserで読み込み、pathは専用SvgPathImporterが
    // M/L/H/V/Q/C/ZをPolylineへ正規化します。S/T/A、複数subpath、transform / gradient / CSS等は後続拡張です。
    static bool ImportFile(const std::string& path, SvgDocument& outDocument, std::string* outError = nullptr);
};

} // namespace Raven
