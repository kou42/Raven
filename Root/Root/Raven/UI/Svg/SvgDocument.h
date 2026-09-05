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

struct SvgCircleElement
{
    std::string Name;
    math::Vec2 Center{};
    float Radius = 0.0f;
    math::Vec4 FillColor{ 1.0f, 1.0f, 1.0f, 1.0f };
};

// SVG Importerが生成するRaven側の中間表現です。
// 描画WidgetとXML解析を分離し、shape種別が増えてもUI Runtime側がXML構文そのものを
// 知る必要がないようにします。現段階ではrect / circleを最小Vector Shapeとして保持します。
struct SvgDocument
{
    math::Vec2 ViewportSize{};
    std::vector<SvgRectElement> Rectangles;
    std::vector<SvgCircleElement> Circles;
    AnimationClip Animation;
    bool LoopAnimation = false;
};

} // namespace Raven
