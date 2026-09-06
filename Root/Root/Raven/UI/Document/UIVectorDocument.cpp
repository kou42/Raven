#include "Raven/UI/Document/UIVectorDocument.h"

#include "Raven/UI/Widgets/UICircle.h"
#include "Raven/UI/Widgets/UIPanel.h"
#include "Raven/UI/Widgets/UIPolygon.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace Raven
{

bool UIVectorDocument::SetDocument(UIDocument document, std::string* outError)
{
    m_AnimationBinding.Clear();
    ClearChildren();
    m_Document = std::move(document);
    m_PlaybackTime = 0.0f;
    m_Playing = false;
    return BuildRuntimeTree(outError);
}

bool UIVectorDocument::BuildRuntimeTree(std::string* outError)
{
    SetSize(m_Document.ViewportSize);
    const VectorDocument& vectorDocument = m_Document.Vector;

    // 型別vectorの順序ではなく、Importerが統合したShapes順で子を追加し、図形の前後関係を維持します。
    for (const VectorElementReference& elementReference : vectorDocument.Shapes)
    {
        Scope<UIElement> element;

        if (elementReference.Type == VectorElementType::Rect)
        {
            if (elementReference.ElementIndex >= vectorDocument.Rectangles.size())
            {
                if (outError != nullptr) { *outError = "Vector rect element reference is out of range."; }
                ClearChildren(); return false;
            }
            const RectElement& data = vectorDocument.Rectangles[elementReference.ElementIndex];
            auto widget = CreateScope<UIPanel>();
            if (widget->SetName(data.Name) == false)
            {
                if (outError != nullptr) { *outError = "Vector element name is invalid: " + data.Name; }
                ClearChildren(); return false;
            }
            widget->SetPosition(data.Position); widget->SetSize(data.Size); widget->SetBackgroundColor(data.FillColor); element = std::move(widget);
        }
        else if (elementReference.Type == VectorElementType::Circle)
        {
            if (elementReference.ElementIndex >= vectorDocument.Circles.size())
            {
                if (outError != nullptr) { *outError = "Vector circle element reference is out of range."; }
                ClearChildren(); return false;
            }
            const CircleElement& data = vectorDocument.Circles[elementReference.ElementIndex]; auto widget = CreateScope<UICircle>();
            if (widget->SetName(data.Name) == false)
            {
                if (outError != nullptr) { *outError = "Vector element name is invalid: " + data.Name; }
                ClearChildren(); return false;
            }
            widget->SetPosition(math::Vec2(data.Center.x - data.Radius, data.Center.y - data.Radius)); widget->SetSize(math::Vec2(data.Radius * 2.0f, data.Radius * 2.0f)); widget->SetFillColor(data.FillColor); element = std::move(widget);
        }
        else if (elementReference.Type == VectorElementType::Ellipse)
        {
            if (elementReference.ElementIndex >= vectorDocument.Ellipses.size())
            {
                if (outError != nullptr) { *outError = "Vector ellipse element reference is out of range."; }
                ClearChildren(); return false;
            }
            const EllipseElement& data = vectorDocument.Ellipses[elementReference.ElementIndex]; auto widget = CreateScope<UICircle>();
            if (widget->SetName(data.Name) == false)
            {
                if (outError != nullptr) { *outError = "Vector element name is invalid: " + data.Name; }
                ClearChildren(); return false;
            }
            widget->SetPosition(math::Vec2(data.Center.x - data.Radius.x, data.Center.y - data.Radius.y)); widget->SetSize(math::Vec2(data.Radius.x * 2.0f, data.Radius.y * 2.0f)); widget->SetFillColor(data.FillColor); element = std::move(widget);
        }
        else if (elementReference.Type == VectorElementType::Line)
        {
            if (elementReference.ElementIndex >= vectorDocument.Lines.size())
            {
                if (outError != nullptr) { *outError = "Vector line element reference is out of range."; }
                ClearChildren(); return false;
            }
            const LineElement& data = vectorDocument.Lines[elementReference.ElementIndex];
            const float dx = data.End.x - data.Start.x; const float dy = data.End.y - data.Start.y; const float length = std::sqrt(dx * dx + dy * dy); const math::Vec2 center((data.Start.x + data.End.x) * 0.5f, (data.Start.y + data.End.y) * 0.5f); auto widget = CreateScope<UIPanel>();
            if (widget->SetName(data.Name) == false)
            {
                if (outError != nullptr) { *outError = "Vector element name is invalid: " + data.Name; }
                ClearChildren(); return false;
            }
            widget->SetSize(math::Vec2(length, data.StrokeWidth)); widget->SetPosition(math::Vec2(center.x - length * 0.5f, center.y - data.StrokeWidth * 0.5f)); widget->SetRotation(std::atan2(dy, dx)); widget->SetBackgroundColor(data.StrokeColor); element = std::move(widget);
        }
        else if (elementReference.Type == VectorElementType::Polygon)
        {
            if (elementReference.ElementIndex >= vectorDocument.Polygons.size())
            {
                if (outError != nullptr) { *outError = "Vector polygon element reference is out of range."; }
                ClearChildren(); return false;
            }

            const PolygonElement& data = vectorDocument.Polygons[elementReference.ElementIndex];
            if (data.Points.size() < 3u)
            {
                if (outError != nullptr) { *outError = "Vector polygon has fewer than three points."; }
                ClearChildren(); return false;
            }

            math::Vec2 min = data.Points[0u];
            math::Vec2 max = data.Points[0u];
            for (const math::Vec2& point : data.Points)
            {
                min.x = std::min(min.x, point.x); min.y = std::min(min.y, point.y); max.x = std::max(max.x, point.x); max.y = std::max(max.y, point.y);
            }
            std::vector<math::Vec2> localPoints; localPoints.reserve(data.Points.size());
            for (const math::Vec2& point : data.Points)
            {
                localPoints.push_back(math::Vec2(point.x - min.x, point.y - min.y));
            }
            auto widget = CreateScope<UIPolygon>();
            if (widget->SetName(data.Name) == false)
            {
                if (outError != nullptr) { *outError = "Vector element name is invalid: " + data.Name; }
                ClearChildren(); return false;
            }
            widget->SetPosition(min); widget->SetSize(math::Vec2(max.x - min.x, max.y - min.y)); widget->SetPoints(std::move(localPoints)); widget->SetFillColor(data.FillColor); element = std::move(widget);
        }
        else if (elementReference.Type == VectorElementType::Path)
        {
            if (elementReference.ElementIndex >= vectorDocument.Paths.size())
            {
                if (outError != nullptr) { *outError = "Vector path element reference is out of range."; }
                ClearChildren(); return false;
            }

            const PathElement& data = vectorDocument.Paths[elementReference.ElementIndex];
            if (data.Subpaths.empty() == true)
            {
                if (outError != nullptr) { *outError = "Vector path has no subpaths."; }
                ClearChildren(); return false;
            }
            if (data.SubpathClosed.size() != data.Subpaths.size())
            {
                if (outError != nullptr) { *outError = "Vector path subpath closed-state count does not match subpath count."; }
                ClearChildren(); return false;
            }

            bool hasBounds = false;
            math::Vec2 min{};
            math::Vec2 max{};
            for (const std::vector<math::Vec2>& subpath : data.Subpaths)
            {
                for (const math::Vec2& point : subpath)
                {
                    if (hasBounds == false)
                    {
                        min = point;
                        max = point;
                        hasBounds = true;
                    }
                    else
                    {
                        min.x = std::min(min.x, point.x); min.y = std::min(min.y, point.y); max.x = std::max(max.x, point.x); max.y = std::max(max.y, point.y);
                    }
                }
            }

            if (hasBounds == false)
            {
                if (outError != nullptr) { *outError = "Vector path has no drawable points."; }
                ClearChildren(); return false;
            }

            // Strokeは中心線の両側へ半幅ずつ張り出すため、Container boundsにもその余白を含めます。
            const bool drawsStroke = data.StrokeColor.w > 0.0f && data.StrokeWidth > 0.0f;
            if (drawsStroke == true)
            {
                const float halfStrokeWidth = data.StrokeWidth * 0.5f;
                min.x -= halfStrokeWidth;
                min.y -= halfStrokeWidth;
                max.x += halfStrokeWidth;
                max.y += halfStrokeWidth;
            }

            const math::Vec2 pathSize(max.x - min.x, max.y - min.y);
            auto pathContainer = CreateScope<UIPanel>();
            if (pathContainer->SetName(data.Name) == false)
            {
                if (outError != nullptr) { *outError = "Vector element name is invalid: " + data.Name; }
                ClearChildren(); return false;
            }
            pathContainer->SetPosition(min);
            pathContainer->SetSize(pathSize);
            pathContainer->SetBackgroundColor(math::Vec4(0.0f, 0.0f, 0.0f, 0.0f));

            // SVGのfillはopen subpathでも暗黙に終点から始点へ閉じて評価されます。
            // UIPolygonも同じ閉領域として扱うため、3点以上ならZ/zの有無に関係なくfillできます。
            if (data.FillColor.w > 0.0f)
            {
                for (const std::vector<math::Vec2>& subpath : data.Subpaths)
                {
                    if (subpath.size() < 3u)
                    {
                        continue;
                    }

                    std::vector<math::Vec2> localPoints;
                    localPoints.reserve(subpath.size());
                    for (const math::Vec2& point : subpath)
                    {
                        localPoints.push_back(math::Vec2(point.x - min.x, point.y - min.y));
                    }

                    auto polygon = CreateScope<UIPolygon>();
                    polygon->SetPosition(math::Vec2(0.0f, 0.0f));
                    polygon->SetSize(pathSize);
                    polygon->SetPoints(std::move(localPoints));
                    polygon->SetFillColor(data.FillColor);
                    if (pathContainer->AddChild(std::move(polygon)) == nullptr)
                    {
                        if (outError != nullptr) { *outError = "Failed to attach Vector path fill subpath to runtime container."; }
                        ClearChildren(); return false;
                    }
                }
            }

            if (drawsStroke == true)
            {
                auto addStrokeSegment = [&pathContainer, &data, &min, outError](
                    const math::Vec2& start,
                    const math::Vec2& end)
                {
                    const float dx = end.x - start.x;
                    const float dy = end.y - start.y;
                    const float length = std::sqrt(dx * dx + dy * dy);
                    if (length <= 0.000001f)
                    {
                        return true;
                    }

                    // 現段階のstrokeは既存LineElementと同じ矩形segment表現を再利用します。
                    // linecap/linejoinは後続のstroke tessellationで置き換えやすいよう、segment生成をここへ集約します。
                    const math::Vec2 localStart(start.x - min.x, start.y - min.y);
                    const math::Vec2 localEnd(end.x - min.x, end.y - min.y);
                    const math::Vec2 center(
                        (localStart.x + localEnd.x) * 0.5f,
                        (localStart.y + localEnd.y) * 0.5f);

                    auto segment = CreateScope<UIPanel>();
                    segment->SetSize(math::Vec2(length, data.StrokeWidth));
                    segment->SetPosition(math::Vec2(
                        center.x - length * 0.5f,
                        center.y - data.StrokeWidth * 0.5f));
                    segment->SetRotation(std::atan2(dy, dx));
                    segment->SetBackgroundColor(data.StrokeColor);
                    if (pathContainer->AddChild(std::move(segment)) == nullptr)
                    {
                        if (outError != nullptr) { *outError = "Failed to attach Vector path stroke segment to runtime container."; }
                        return false;
                    }
                    return true;
                };

                for (std::size_t subpathIndex = 0u; subpathIndex < data.Subpaths.size(); ++subpathIndex)
                {
                    const std::vector<math::Vec2>& subpath = data.Subpaths[subpathIndex];
                    if (subpath.size() < 2u)
                    {
                        continue;
                    }

                    for (std::size_t pointIndex = 1u; pointIndex < subpath.size(); ++pointIndex)
                    {
                        if (addStrokeSegment(subpath[pointIndex - 1u], subpath[pointIndex]) == false)
                        {
                            ClearChildren(); return false;
                        }
                    }

                    // Z/zがあるsubpathだけ終点から始点へのstroke segmentを追加します。
                    // open pathではこのsegmentを作らないことが、fillとの最も重要な意味差です。
                    if (data.SubpathClosed[subpathIndex] == true)
                    {
                        if (addStrokeSegment(subpath.back(), subpath.front()) == false)
                        {
                            ClearChildren(); return false;
                        }
                    }
                }
            }

            element = std::move(pathContainer);
        }

        if (element == nullptr)
        {
            if (outError != nullptr) { *outError = "Vector element type is unsupported by runtime."; }
            ClearChildren(); return false;
        }
        if (AddChild(std::move(element)) == nullptr)
        {
            if (outError != nullptr) { *outError = "Failed to attach Vector element to UI tree."; }
            ClearChildren(); return false;
        }
    }

    if (m_Document.Animation.GetPropertyTrackCount() > 0u)
    {
        if (m_AnimationBinding.Resolve(*this, m_Document.Animation) == false)
        {
            if (outError != nullptr) { *outError = "Failed to resolve Vector animation bindings."; }
            ClearChildren(); return false;
        }
        if (ApplyAnimation() == false)
        {
            if (outError != nullptr) { *outError = "Failed to apply initial Vector animation pose."; }
            ClearChildren(); return false;
        }
    }
    return true;
}

void UIVectorDocument::Play() { m_Playing = true; }
void UIVectorDocument::Pause() { m_Playing = false; }
void UIVectorDocument::Stop() { m_Playing = false; m_PlaybackTime = 0.0f; ApplyAnimation(); }

void UIVectorDocument::Update(float deltaTime)
{
    if (m_Playing == false || deltaTime <= 0.0f) { return; }
    const float duration = m_Document.Animation.GetDuration();
    if (duration <= 0.0f) { m_Playing = false; return; }
    m_PlaybackTime += deltaTime;
    if (m_Document.LoopAnimation == true) { m_PlaybackTime = std::fmod(m_PlaybackTime, duration); }
    else if (m_PlaybackTime >= duration) { m_PlaybackTime = duration; m_Playing = false; }
    ApplyAnimation();
}

bool UIVectorDocument::ApplyAnimation()
{
    if (m_Document.Animation.GetPropertyTrackCount() == 0u) { return true; }
    if (m_AnimationBinding.IsResolved() == true && m_AnimationBinding.Apply(m_Document.Animation, m_PlaybackTime) == true) { return true; }
    if (m_AnimationBinding.Resolve(*this, m_Document.Animation) == false) { return false; }
    return m_AnimationBinding.Apply(m_Document.Animation, m_PlaybackTime);
}

} // namespace Raven
