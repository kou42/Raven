#include "Raven/UI/Svg/UISvg.h"

#include "Raven/UI/Svg/SvgImporter.h"
#include "Raven/UI/Svg/SvgPathImporter.h"
#include "Raven/UI/Widgets/UICircle.h"
#include "Raven/UI/Widgets/UIPanel.h"
#include "Raven/UI/Widgets/UIPolygon.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace Raven
{

bool UISvg::LoadFromFile(const std::string& path, std::string* outError)
{
    SvgDocument imported;
    if (SvgImporter::ImportFile(path, imported, outError) == false)
    {
        return false;
    }

    // path command grammarは専用ParserでPolylineへ正規化します。
    // 既存shape Importerと同じSvgDocumentへ追加し、SourceOffsetで描画順を再統合します。
    if (SvgPathImporter::AppendFilePaths(path, imported, outError) == false)
    {
        return false;
    }

    m_AnimationBinding.Clear();
    ClearChildren();
    m_Document = std::move(imported);
    m_PlaybackTime = 0.0f;
    m_Playing = false;

    return BuildRuntimeTree(outError);
}

bool UISvg::BuildRuntimeTree(std::string* outError)
{
    SetSize(m_Document.ViewportSize);

    for (const SvgShapeReference& shape : m_Document.Shapes)
    {
        Scope<UIElement> element;

        if (shape.Type == SvgShapeType::Rect)
        {
            if (shape.ElementIndex >= m_Document.Rectangles.size())
            {
                if (outError != nullptr) { *outError = "SVG rect shape reference is out of range."; }
                ClearChildren(); return false;
            }
            const SvgRectElement& data = m_Document.Rectangles[shape.ElementIndex];
            auto widget = CreateScope<UIPanel>();
            if (widget->SetName(data.Name) == false)
            {
                if (outError != nullptr) { *outError = "SVG element name is invalid: " + data.Name; }
                ClearChildren(); return false;
            }
            widget->SetPosition(data.Position); widget->SetSize(data.Size); widget->SetBackgroundColor(data.FillColor); element = std::move(widget);
        }
        else if (shape.Type == SvgShapeType::Circle)
        {
            if (shape.ElementIndex >= m_Document.Circles.size())
            {
                if (outError != nullptr) { *outError = "SVG circle shape reference is out of range."; }
                ClearChildren(); return false;
            }
            const SvgCircleElement& data = m_Document.Circles[shape.ElementIndex]; auto widget = CreateScope<UICircle>();
            if (widget->SetName(data.Name) == false)
            {
                if (outError != nullptr) { *outError = "SVG element name is invalid: " + data.Name; }
                ClearChildren(); return false;
            }
            widget->SetPosition(math::Vec2(data.Center.x - data.Radius, data.Center.y - data.Radius)); widget->SetSize(math::Vec2(data.Radius * 2.0f, data.Radius * 2.0f)); widget->SetFillColor(data.FillColor); element = std::move(widget);
        }
        else if (shape.Type == SvgShapeType::Ellipse)
        {
            if (shape.ElementIndex >= m_Document.Ellipses.size())
            {
                if (outError != nullptr) { *outError = "SVG ellipse shape reference is out of range."; }
                ClearChildren(); return false;
            }
            const SvgEllipseElement& data = m_Document.Ellipses[shape.ElementIndex]; auto widget = CreateScope<UICircle>();
            if (widget->SetName(data.Name) == false)
            {
                if (outError != nullptr) { *outError = "SVG element name is invalid: " + data.Name; }
                ClearChildren(); return false;
            }
            widget->SetPosition(math::Vec2(data.Center.x - data.Radius.x, data.Center.y - data.Radius.y)); widget->SetSize(math::Vec2(data.Radius.x * 2.0f, data.Radius.y * 2.0f)); widget->SetFillColor(data.FillColor); element = std::move(widget);
        }
        else if (shape.Type == SvgShapeType::Line)
        {
            if (shape.ElementIndex >= m_Document.Lines.size())
            {
                if (outError != nullptr) { *outError = "SVG line shape reference is out of range."; }
                ClearChildren(); return false;
            }
            const SvgLineElement& data = m_Document.Lines[shape.ElementIndex]; const float dx = data.End.x - data.Start.x; const float dy = data.End.y - data.Start.y; const float length = std::sqrt(dx * dx + dy * dy); const math::Vec2 center((data.Start.x + data.End.x) * 0.5f, (data.Start.y + data.End.y) * 0.5f); auto widget = CreateScope<UIPanel>();
            if (widget->SetName(data.Name) == false)
            {
                if (outError != nullptr) { *outError = "SVG element name is invalid: " + data.Name; }
                ClearChildren(); return false;
            }
            widget->SetSize(math::Vec2(length, data.StrokeWidth)); widget->SetPosition(math::Vec2(center.x - length * 0.5f, center.y - data.StrokeWidth * 0.5f)); widget->SetRotation(std::atan2(dy, dx)); widget->SetBackgroundColor(data.StrokeColor); element = std::move(widget);
        }
        else if (shape.Type == SvgShapeType::Polygon || shape.Type == SvgShapeType::Path)
        {
            const std::vector<math::Vec2>* points = nullptr; const math::Vec4* fillColor = nullptr; const std::string* name = nullptr;
            if (shape.Type == SvgShapeType::Polygon)
            {
                if (shape.ElementIndex >= m_Document.Polygons.size())
                {
                    if (outError != nullptr) { *outError = "SVG polygon shape reference is out of range."; }
                    ClearChildren(); return false;
                }
                const SvgPolygonElement& data = m_Document.Polygons[shape.ElementIndex]; points = &data.Points; fillColor = &data.FillColor; name = &data.Name;
            }
            else
            {
                if (shape.ElementIndex >= m_Document.Paths.size())
                {
                    if (outError != nullptr) { *outError = "SVG path shape reference is out of range."; }
                    ClearChildren(); return false;
                }
                const SvgPathElement& data = m_Document.Paths[shape.ElementIndex]; points = &data.Points; fillColor = &data.FillColor; name = &data.Name;
            }
            if (points == nullptr || fillColor == nullptr || name == nullptr || points->size() < 3u)
            {
                if (outError != nullptr) { *outError = "SVG polygon/path has fewer than three points."; }
                ClearChildren(); return false;
            }
            math::Vec2 min = (*points)[0u]; math::Vec2 max = (*points)[0u];
            for (const math::Vec2& point : *points)
            {
                min.x = std::min(min.x, point.x); min.y = std::min(min.y, point.y); max.x = std::max(max.x, point.x); max.y = std::max(max.y, point.y);
            }
            std::vector<math::Vec2> localPoints; localPoints.reserve(points->size());
            for (const math::Vec2& point : *points)
            {
                localPoints.push_back(math::Vec2(point.x - min.x, point.y - min.y));
            }
            auto widget = CreateScope<UIPolygon>();
            if (widget->SetName(*name) == false)
            {
                if (outError != nullptr) { *outError = "SVG element name is invalid: " + *name; }
                ClearChildren(); return false;
            }
            widget->SetPosition(min); widget->SetSize(math::Vec2(max.x - min.x, max.y - min.y)); widget->SetPoints(std::move(localPoints)); widget->SetFillColor(*fillColor); element = std::move(widget);
        }

        if (element == nullptr)
        {
            if (outError != nullptr) { *outError = "SVG shape type is unsupported by UISvg runtime."; }
            ClearChildren(); return false;
        }
        if (AddChild(std::move(element)) == nullptr)
        {
            if (outError != nullptr) { *outError = "Failed to attach SVG shape to UI tree."; }
            ClearChildren(); return false;
        }
    }

    if (m_Document.Animation.GetPropertyTrackCount() > 0u)
    {
        if (m_AnimationBinding.Resolve(*this, m_Document.Animation) == false)
        {
            if (outError != nullptr) { *outError = "Failed to resolve imported SVG animation bindings."; }
            ClearChildren(); return false;
        }
        if (ApplyAnimation() == false)
        {
            if (outError != nullptr) { *outError = "Failed to apply initial SVG animation pose."; }
            ClearChildren(); return false;
        }
    }
    return true;
}

void UISvg::Play() { m_Playing = true; }
void UISvg::Pause() { m_Playing = false; }
void UISvg::Stop() { m_Playing = false; m_PlaybackTime = 0.0f; ApplyAnimation(); }

void UISvg::Update(float deltaTime)
{
    if (m_Playing == false || deltaTime <= 0.0f) { return; }
    const float duration = m_Document.Animation.GetDuration();
    if (duration <= 0.0f) { m_Playing = false; return; }
    m_PlaybackTime += deltaTime;
    if (m_Document.LoopAnimation == true) { m_PlaybackTime = std::fmod(m_PlaybackTime, duration); }
    else if (m_PlaybackTime >= duration) { m_PlaybackTime = duration; m_Playing = false; }
    ApplyAnimation();
}

bool UISvg::ApplyAnimation()
{
    if (m_Document.Animation.GetPropertyTrackCount() == 0u) { return true; }
    if (m_AnimationBinding.IsResolved() == true && m_AnimationBinding.Apply(m_Document.Animation, m_PlaybackTime) == true) { return true; }
    if (m_AnimationBinding.Resolve(*this, m_Document.Animation) == false) { return false; }
    return m_AnimationBinding.Apply(m_Document.Animation, m_PlaybackTime);
}

} // namespace Raven
