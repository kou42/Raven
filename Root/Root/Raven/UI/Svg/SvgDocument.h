#pragma once

#include "Raven/Animation/AnimationClip.h"
#include "Raven/Math/MathVector.h"

#include <cstddef>
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

struct SvgEllipseElement
{
    std::string Name;
    math::Vec2 Center{};
    math::Vec2 Radius{};
    math::Vec4 FillColor{ 1.0f, 1.0f, 1.0f, 1.0f };
};

struct SvgLineElement
{
    std::string Name;
    math::Vec2 Start{};
    math::Vec2 End{};
    math::Vec4 StrokeColor{ 0.0f, 0.0f, 0.0f, 1.0f };
    float StrokeWidth = 1.0f;
};

struct SvgPolygonElement
{
    std::string Name;
    std::vector<math::Vec2> Points;
    math::Vec4 FillColor{ 1.0f, 1.0f, 1.0f, 1.0f };
};

enum class SvgFillRule
{
    NonZero,
    EvenOdd
};

enum class SvgLineCap
{
    Butt,
    Round,
    Square
};

enum class SvgLineJoin
{
    Miter,
    Round,
    Bevel
};

// SVG pathは1つのelement内に複数subpathを持ち、各subpathはopen/closedの状態を個別に保持します。
// command文字列はImporterでPolylineへ正規化し、UI RuntimeはSVG構文を知らず輪郭列と描画styleだけを扱います。
struct SvgPathElement
{
    std::string Name;
    std::vector<std::vector<math::Vec2>> Subpaths;
    std::vector<bool> SubpathClosed;
    math::Vec4 FillColor{ 0.0f, 0.0f, 0.0f, 1.0f };
    SvgFillRule FillRule = SvgFillRule::NonZero;
    // SVGのstroke既定値はnone、linecapはbutt、linejoinはmiter、miterlimitは4です。
    math::Vec4 StrokeColor{ 0.0f, 0.0f, 0.0f, 0.0f };
    float StrokeWidth = 1.0f;
    SvgLineCap StrokeLineCap = SvgLineCap::Butt;
    SvgLineJoin StrokeLineJoin = SvgLineJoin::Miter;
    float StrokeMiterLimit = 4.0f;
};

enum class SvgShapeType
{
    Rect,
    Circle,
    Ellipse,
    Line,
    Polygon,
    Path
};

// shape本体は型別vectorへ保持しつつ、SVGソース中の出現順だけを軽量な参照列として保持します。
// SVGでは後に記述されたshapeほど前面へ描画されるため、この順序情報を失わないことが重要です。
// line / polygon / pathのようにshape種別が増えても同じ参照列へ型とindexを追加するだけで描画順を維持できます。
struct SvgShapeReference
{
    SvgShapeType Type = SvgShapeType::Rect;
    std::size_t ElementIndex = 0u;
    std::size_t SourceOffset = 0u;
};

// SVG Importerが生成するRaven側の中間表現です。
// 描画WidgetとXML解析を分離し、shape種別が増えてもUI Runtime側がXML構文そのものを
// 知る必要がないようにします。Shapesは元XML順を保持し、型別vectorは各shape固有dataを所有します。
struct SvgDocument
{
    math::Vec2 ViewportSize{};
    std::vector<SvgRectElement> Rectangles;
    std::vector<SvgCircleElement> Circles;
    std::vector<SvgEllipseElement> Ellipses;
    std::vector<SvgLineElement> Lines;
    std::vector<SvgPolygonElement> Polygons;
    std::vector<SvgPathElement> Paths;
    std::vector<SvgShapeReference> Shapes;
    AnimationClip Animation;
    bool LoopAnimation = false;
};

} // namespace Raven
