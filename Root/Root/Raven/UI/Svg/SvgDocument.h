#pragma once

#include "Raven/Animation/AnimationClip.h"
#include "Raven/Math/MathVector.h"

#include <string>
#include <vector>

namespace Raven
{

struct SvgRectElement
{
    std::string Name;
    math::Vec2 Position{};
    math::Vec2 Size{};
    math::Vec4 FillColor{ 1.0f, 1.0f, 1.0f, 1.0f };
};

// SVG Importerが生成するRaven側の中間表現です。
// 描画WidgetとXML解析を分離し、将来circle/path等を追加してもUI Runtime側が
// XML構文そのものを知る必要がないようにします。
struct SvgDocument
{
    math::Vec2 ViewportSize{};
    std::vector<SvgRectElement> Rectangles;
    AnimationClip Animation;
    bool LoopAnimation = false;
};

} // namespace Raven
