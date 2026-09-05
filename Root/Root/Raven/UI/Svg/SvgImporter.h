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
    // M/L/H/V/Q/T/C/S/A/Zを複数Polylineへ正規化し、open/closed・fill-rule・stroke・stroke-miterlimitを保持します。
    // pathのstroke-linecap / stroke-linejoin直接属性はUISvg読込時に適用し、transform / gradient / CSS / style属性等は後続拡張です。
    static bool ImportFile(const std::string& path, SvgDocument& outDocument, std::string* outError = nullptr);
};

} // namespace Raven
