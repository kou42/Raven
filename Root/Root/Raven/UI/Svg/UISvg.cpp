#include "Raven/UI/Svg/UISvg.h"

#include "Raven/UI/Svg/SvgImporter.h"
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

    // SVGは同一親内で後に記述されたshapeほど前面へ描画されます。
    // Importerが保持した元XML順でUIElementをAddChildすることで、Retained Treeのchild順と
    // DrawList生成順をSVGのPainter's Algorithmへそのまま対応させます。
    for (const SvgShapeReference& shape : m_Document.Shapes)
    {
        Scope<UIElement> element;

        if (shape.Type == SvgShapeType::Rect)
        {
            if (shape.ElementIndex >= m_Document.Rectangles.size())
            {
                if (outError != nullptr)
                {
                    *outError = "SVG rect shape reference is out of range.";
                }
                ClearChildren();
                return false;
            }

            const SvgRectElement& data = m_Document.Rectangles[shape.ElementIndex];
            auto widget = CreateScope<UIPanel>();
            if (widget->SetName(data.Name) == false)
            {
                if (outError != nullptr)
                {
                    *outError = "SVG element name is invalid: " + data.Name;
                }
                ClearChildren();
                return false;
            }

            widget->SetPosition(data.Position);
            widget->SetSize(data.Size);
            widget->SetBackgroundColor(data.FillColor);
            element = std::move(widget);
        }
        else if (shape.Type == SvgShapeType::Circle)
        {
            if (shape.ElementIndex >= m_Document.Circles.size())
            {
                if (outError != nullptr)
                {
                    *outError = "SVG circle shape reference is out of range.";
                }
                ClearChildren();
                return false;
            }

            const SvgCircleElement& data = m_Document.Circles[shape.ElementIndex];
            auto widget = CreateScope<UICircle>();
            if (widget->SetName(data.Name) == false)
            {
                if (outError != nullptr)
                {
                    *outError = "SVG element name is invalid: " + data.Name;
                }
                ClearChildren();
                return false;
            }

            // SVG circleはcenter/radius、Raven UIは左上Position/SizeでLayoutするためここで変換します。
            // Animation Importerも同じ変換規則でPosition/Size Trackを生成し、初期PoseとRuntime Poseを一致させます。
            widget->SetPosition(math::Vec2(
                data.Center.x - data.Radius,
                data.Center.y - data.Radius));
            widget->SetSize(math::Vec2(
                data.Radius * 2.0f,
                data.Radius * 2.0f));
            widget->SetFillColor(data.FillColor);
            element = std::move(widget);
        }
        else if (shape.Type == SvgShapeType::Ellipse)
        {
            if (shape.ElementIndex >= m_Document.Ellipses.size())
            {
                if (outError != nullptr)
                {
                    *outError = "SVG ellipse shape reference is out of range.";
                }
                ClearChildren();
                return false;
            }

            const SvgEllipseElement& data = m_Document.Ellipses[shape.ElementIndex];
            auto widget = CreateScope<UICircle>();
            if (widget->SetName(data.Name) == false)
            {
                if (outError != nullptr)
                {
                    *outError = "SVG element name is invalid: " + data.Name;
                }
                ClearChildren();
                return false;
            }

            // UICircleは非正方BoundsもradiusX/radiusYで描画するため、そのままellipseとして再利用できます。
            widget->SetPosition(math::Vec2(
                data.Center.x - data.Radius.x,
                data.Center.y - data.Radius.y));
            widget->SetSize(math::Vec2(
                data.Radius.x * 2.0f,
                data.Radius.y * 2.0f));
            widget->SetFillColor(data.FillColor);
            element = std::move(widget);
        }
        else if (shape.Type == SvgShapeType::Line)
        {
            if (shape.ElementIndex >= m_Document.Lines.size())
            {
                if (outError != nullptr)
                {
                    *outError = "SVG line shape reference is out of range.";
                }
                ClearChildren();
                return false;
            }

            const SvgLineElement& data = m_Document.Lines[shape.ElementIndex];
            const float deltaX = data.End.x - data.Start.x;
            const float deltaY = data.End.y - data.Start.y;
            const float length = std::sqrt(deltaX * deltaX + deltaY * deltaY);
            const math::Vec2 center(
                (data.Start.x + data.End.x) * 0.5f,
                (data.Start.y + data.End.y) * 0.5f);

            auto widget = CreateScope<UIPanel>();
            if (widget->SetName(data.Name) == false)
            {
                if (outError != nullptr)
                {
                    *outError = "SVG element name is invalid: " + data.Name;
                }
                ClearChildren();
                return false;
            }

            // line専用GPU Primitiveは増やさず、長さ×stroke-widthのUIPanelを中心pivotで回転させます。
            // Animation Importerも同じPosition/Size/Rotationへ変換するため既存UIAnimationBindingを再利用できます。
            widget->SetSize(math::Vec2(length, data.StrokeWidth));
            widget->SetPosition(math::Vec2(
                center.x - length * 0.5f,
                center.y - data.StrokeWidth * 0.5f));
            widget->SetRotation(std::atan2(deltaY, deltaX));
            widget->SetBackgroundColor(data.StrokeColor);
            element = std::move(widget);
        }
        else if (shape.Type == SvgShapeType::Polygon)
        {
            if (shape.ElementIndex >= m_Document.Polygons.size())
            {
                if (outError != nullptr)
                {
                    *outError = "SVG polygon shape reference is out of range.";
                }
                ClearChildren();
                return false;
            }

            const SvgPolygonElement& data = m_Document.Polygons[shape.ElementIndex];
            if (data.Points.size() < 3u)
            {
                if (outError != nullptr)
                {
                    *outError = "SVG polygon has fewer than three points.";
                }
                ClearChildren();
                return false;
            }

            math::Vec2 min = data.Points[0u];
            math::Vec2 max = data.Points[0u];
            for (const math::Vec2& point : data.Points)
            {
                min.x = std::min(min.x, point.x);
                min.y = std::min(min.y, point.y);
                max.x = std::max(max.x, point.x);
                max.y = std::max(max.y, point.y);
            }

            // UIPolygonはElement左上をLocal原点として保持するため、SVG絶対座標からBounds最小値を引きます。
            // こうしておくと通常のUI Position/Transformと任意頂点Geometryを同じRetained Tree上で扱えます。
            std::vector<math::Vec2> localPoints;
            localPoints.reserve(data.Points.size());
            for (const math::Vec2& point : data.Points)
            {
                localPoints.push_back(math::Vec2(
                    point.x - min.x,
                    point.y - min.y));
            }

            auto widget = CreateScope<UIPolygon>();
            if (widget->SetName(data.Name) == false)
            {
                if (outError != nullptr)
                {
                    *outError = "SVG element name is invalid: " + data.Name;
                }
                ClearChildren();
                return false;
            }

            widget->SetPosition(min);
            widget->SetSize(math::Vec2(max.x - min.x, max.y - min.y));
            widget->SetPoints(std::move(localPoints));
            widget->SetFillColor(data.FillColor);
            element = std::move(widget);
        }

        if (element == nullptr)
        {
            if (outError != nullptr)
            {
                *outError = "SVG shape type is unsupported by UISvg runtime.";
            }
            ClearChildren();
            return false;
        }

        if (AddChild(std::move(element)) == nullptr)
        {
            if (outError != nullptr)
            {
                *outError = "Failed to attach SVG shape to UI tree.";
            }
            ClearChildren();
            return false;
        }
    }

    if (m_Document.Animation.GetPropertyTrackCount() > 0u)
    {
        if (m_AnimationBinding.Resolve(*this, m_Document.Animation) == false)
        {
            if (outError != nullptr)
            {
                *outError = "Failed to resolve imported SVG animation bindings.";
            }
            ClearChildren();
            return false;
        }

        if (ApplyAnimation() == false)
        {
            if (outError != nullptr)
            {
                *outError = "Failed to apply initial SVG animation pose.";
            }
            ClearChildren();
            return false;
        }
    }

    return true;
}

void UISvg::Play()
{
    m_Playing = true;
}

void UISvg::Pause()
{
    m_Playing = false;
}

void UISvg::Stop()
{
    m_Playing = false;
    m_PlaybackTime = 0.0f;
    ApplyAnimation();
}

void UISvg::Update(float deltaTime)
{
    if (m_Playing == false || deltaTime <= 0.0f)
    {
        return;
    }

    const float duration = m_Document.Animation.GetDuration();
    if (duration <= 0.0f)
    {
        m_Playing = false;
        return;
    }

    m_PlaybackTime += deltaTime;
    if (m_Document.LoopAnimation == true)
    {
        m_PlaybackTime = std::fmod(m_PlaybackTime, duration);
    }
    else if (m_PlaybackTime >= duration)
    {
        m_PlaybackTime = duration;
        m_Playing = false;
    }

    ApplyAnimation();
}

bool UISvg::ApplyAnimation()
{
    if (m_Document.Animation.GetPropertyTrackCount() == 0u)
    {
        return true;
    }

    // LoadFromFile()時点ではUISvgがまだ親Treeへ接続されていない場合があります。
    // UIAnimationBindingはTree Generationを記録するため、その後RootへAddChild()されると
    // 正しいTarget pointerを保持していてもGeneration不一致でApply()が失敗します。
    // Treeが変化したframeだけ再Resolveし、通常frameは解決済みpointerを使うことで既存設計を維持します。
    if (m_AnimationBinding.IsResolved() == true &&
        m_AnimationBinding.Apply(m_Document.Animation, m_PlaybackTime) == true)
    {
        return true;
    }

    if (m_AnimationBinding.Resolve(*this, m_Document.Animation) == false)
    {
        return false;
    }

    return m_AnimationBinding.Apply(m_Document.Animation, m_PlaybackTime);
}

} // namespace Raven
