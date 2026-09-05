#pragma once

#include "Raven/UI/Document/VectorDocument.h"

namespace Raven
{

// SVG Importerの既存APIを保ったまま、実体はファイル形式非依存のVector Elementへ統合します。
// SvgImporter / SvgPathImporter側はSVG構文の解析だけを担当し、生成後のデータは他形式のImporterとも共有できます。
using SvgRectElement = RectElement;
using SvgCircleElement = CircleElement;
using SvgEllipseElement = EllipseElement;
using SvgLineElement = LineElement;
using SvgPolygonElement = PolygonElement;
using SvgPathElement = PathElement;
using SvgShapeType = VectorElementType;
using SvgShapeReference = VectorElementReference;
using SvgDocument = VectorDocument;

} // namespace Raven
