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
    // M/L/H/V/Q/T/C/S/A/Zを複数Polylineへ正規化し、open/closed・fill-rule・stroke直接属性を保持します。
    // transform / gradient / CSS / style属性、stroke-linecap / stroke-linejoin等は後続拡張です。
    static bool ImportFile(const std::string& path, SvgDocument& outDocument, std::string* outError = nullptr);
};

} // namespace Raven
