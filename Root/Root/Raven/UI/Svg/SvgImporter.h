#pragma once

#include "Raven/UI/Svg/SvgDocument.h"

#include <string>

namespace Raven
{

class SvgImporter
{
public:
    // 現段階ではSVG 1.1全体ではなく、Raven UIへ安全に取り込める最小subsetとして
    // <svg>、<rect>、<animate attributeName=\"x|y|width|height|opacity\"> を扱います。
    static bool ImportFile(const std::string& path, SvgDocument& outDocument, std::string* outError = nullptr);
};

} // namespace Raven
