#pragma once

#include "Raven/UI/Text/UIFontAtlas.h"
#include "Raven/UI/Text/UITextMetrics.h"

#include <string_view>

namespace Raven
{

// GPUやDrawListを使用せず、AppendTextと同じFallback/改行規則で論理サイズを測定します。
class UITextMeasurement
{
public:
    static UITextMetrics Measure(const UIFontAtlas& font, std::string_view text, float lineHeight);
};

} // namespace Raven
