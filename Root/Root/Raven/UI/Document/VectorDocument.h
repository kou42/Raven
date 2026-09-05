#pragma once

#include "Raven/Animation/AnimationClip.h"
#include "Raven/Math/MathVector.h"

#include <cstddef>
#include <string>
#include <vector>

namespace Raven
{

// Import元のファイル形式に依存しない、Raven UI向けのVector Element表現です。
// SVGなど各Importerは固有構文をここへ正規化し、Runtime側へファイル形式の詳細を持ち込みません。
struct RectElement
{
    std::string Name;
    math::Vec2 Position{};
    math::Vec2 Size{};
    math::Vec4 FillColor{ 1.0f, 1.0f, 1.0f, 1.0f };
};

struct CircleElement
{
    std::string Name;
    math::Vec2 Center{};
    float Radius = 0.0f;
    math::Vec4 FillColor{ 1.0f, 1.0f, 1.0f, 1.0f };
};

struct EllipseElement
{
    std::string Name;
    math::Vec2 Center{};
    math::Vec2 Radius{};
    math::Vec4 FillColor{ 1.0f, 1.0f, 1.0f, 1.0f };
};

struct LineElement
{
    std::string Name;
    math::Vec2 Start{};
    math::Vec2 End{};
    math::Vec4 StrokeColor{ 0.0f, 0.0f, 0.0f, 1.0f };
    float StrokeWidth = 1.0f;
};

struct PolygonElement
{
    std::string Name;
    std::vector<math::Vec2> Points;
    math::Vec4 FillColor{ 1.0f, 1.0f, 1.0f, 1.0f };
};

// PathはImporter側で曲線command等をPolylineへ正規化した後の共通表現だけを保持します。
// そのためSVG path grammarなど、特定フォーマットの構文情報はここへ持ち込みません。
struct PathElement
{
    std::string Name;
    std::vector<math::Vec2> Points;
    math::Vec4 FillColor{ 0.0f, 0.0f, 0.0f, 1.0f };
};

enum class VectorElementType
{
    Rect,
    Circle,
    Ellipse,
    Line,
    Polygon,
    Path
};

// Element本体は型別vectorが所有し、この参照列は描画順だけを保持します。
// SourceOffsetは現在のSVG Importerが複数Parserの結果を元ソース順へ再統合するために使用します。
// 将来Importerが単一pass化された場合は、Shapes自体の順序へ一本化できます。
struct VectorElementReference
{
    VectorElementType Type = VectorElementType::Rect;
    std::size_t ElementIndex = 0u;
    std::size_t SourceOffset = 0u;
};

// Raven内部で共有するVector Documentです。
// ファイル形式固有のParserはこの形式へ変換し、描画・Animation基盤は共通データだけを扱います。
struct VectorDocument
{
    math::Vec2 ViewportSize{};
    std::vector<RectElement> Rectangles;
    std::vector<CircleElement> Circles;
    std::vector<EllipseElement> Ellipses;
    std::vector<LineElement> Lines;
    std::vector<PolygonElement> Polygons;
    std::vector<PathElement> Paths;
    std::vector<VectorElementReference> Shapes;
    AnimationClip Animation;
    bool LoopAnimation = false;
};

} // namespace Raven
