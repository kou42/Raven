#include "Raven/UI/Text/UITextMeasurement.h"

#include "Raven/UI/Text/UITextLayout.h"

namespace Raven
{

UITextMetrics UITextMeasurement::Measure(const UIFontAtlas& font, std::string_view text, float lineHeight)
{
    // 描画と同一の配置結果を使い、Fallbackや改行規則の乖離を防ぎます。
    return UITextLayout::Build(font, text, lineHeight).Metrics;
}

} // namespace Raven
