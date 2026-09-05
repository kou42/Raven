#include "Raven/UI/Svg/SvgPathImporter.h"

#include "Raven/Animation/AnimationKeyframe.h"
#include "Raven/Animation/AnimationTrack.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <regex>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace Raven
{

namespace
{
using AttributeMap = std::unordered_map<std::string, std::string>;

constexpr float kBezierFlatnessTolerance = 0.5f;
constexpr uint32_t kBezierMaxSubdivisionDepth = 12u;
constexpr float kPointMergeEpsilonSquared = 0.000001f;

AttributeMap ParseAttributes(const std::string& text)
{
    AttributeMap attributes;
    const std::regex attributeRegex(
        R"REGEX(([A-Za-z_:][A-Za-z0-9_.:-]*)\s*=\s*"([^"]*)")REGEX"
    );
    for (std::sregex_iterator it(text.begin(), text.end(), attributeRegex), end; it != end; ++it)
    {
        attributes[(*it)[1].str()] = (*it)[2].str();
    }
    return attributes;
}

bool ReadTextFile(const std::string& path, std::string& outText)
{
    std::ifstream stream(path, std::ios::binary);
    if (stream.is_open() == false)
    {
        return false;
    }

    std::ostringstream buffer;
    buffer << stream.rdbuf();
    outText = buffer.str();
    return true;
}

void SkipSeparators(const std::string& data, std::size_t& cursor)
{
    while (cursor < data.size())
    {
        const unsigned char value = static_cast<unsigned char>(data[cursor]);
        if (std::isspace(value) == 0 && data[cursor] != ',')
        {
            break;
        }
        ++cursor;
    }
}

bool TryReadNumber(const std::string& data, std::size_t& cursor, float& outValue)
{
    SkipSeparators(data, cursor);
    if (cursor >= data.size())
    {
        return false;
    }

    const char* begin = data.c_str() + cursor;
    char* end = nullptr;
    outValue = std::strtof(begin, &end);
    if (end == begin)
    {
        return false;
    }

    cursor += static_cast<std::size_t>(end - begin);
    return true;
}

math::Vec2 Midpoint(const math::Vec2& left, const math::Vec2& right)
{
    return math::Vec2(
        (left.x + right.x) * 0.5f,
        (left.y + right.y) * 0.5f);
}

math::Vec2 ReflectControlPoint(
    const math::Vec2& current,
    const math::Vec2& previousControl)
{
    return math::Vec2(
        current.x * 2.0f - previousControl.x,
        current.y * 2.0f - previousControl.y);
}

float DistanceSquared(const math::Vec2& left, const math::Vec2& right)
{
    const float x = left.x - right.x;
    const float y = left.y - right.y;
    return x * x + y * y;
}

float DistanceToLineSquared(
    const math::Vec2& point,
    const math::Vec2& lineStart,
    const math::Vec2& lineEnd)
{
    const float lineX = lineEnd.x - lineStart.x;
    const float lineY = lineEnd.y - lineStart.y;
    const float lengthSquared = lineX * lineX + lineY * lineY;
    if (lengthSquared <= kPointMergeEpsilonSquared)
    {
        return DistanceSquared(point, lineStart);
    }

    const float pointX = point.x - lineStart.x;
    const float pointY = point.y - lineStart.y;
    const float cross = lineX * pointY - lineY * pointX;
    return (cross * cross) / lengthSquared;
}

void AppendPointIfDistinct(
    std::vector<math::Vec2>& points,
    const math::Vec2& point)
{
    if (points.empty() == false &&
        DistanceSquared(points.back(), point) <= kPointMergeEpsilonSquared)
    {
        return;
    }
    points.push_back(point);
}

void TessellateQuadraticBezier(
    const math::Vec2& start,
    const math::Vec2& control,
    const math::Vec2& end,
    uint32_t depth,
    std::vector<math::Vec2>& outPoints)
{
    const float toleranceSquared =
        kBezierFlatnessTolerance * kBezierFlatnessTolerance;

    // 制御点が始点-終点の弦へ十分近ければ、この区間を直線として扱えます。
    // 固定segment数ではなく局所曲率に応じて再帰分割するため、直線に近いBezierでは頂点数を抑え、
    // 強く曲がる部分だけ細かく分割できます。最大深度は異常入力に対する安全弁です。
    if (depth >= kBezierMaxSubdivisionDepth ||
        DistanceToLineSquared(control, start, end) <= toleranceSquared)
    {
        AppendPointIfDistinct(outPoints, end);
        return;
    }

    const math::Vec2 startControl = Midpoint(start, control);
    const math::Vec2 controlEnd = Midpoint(control, end);
    const math::Vec2 middle = Midpoint(startControl, controlEnd);

    TessellateQuadraticBezier(
        start,
        startControl,
        middle,
        depth + 1u,
        outPoints);
    TessellateQuadraticBezier(
        middle,
        controlEnd,
        end,
        depth + 1u,
        outPoints);
}

void TessellateCubicBezier(
    const math::Vec2& start,
    const math::Vec2& control1,
    const math::Vec2& control2,
    const math::Vec2& end,
    uint32_t depth,
    std::vector<math::Vec2>& outPoints)
{
    const float toleranceSquared =
        kBezierFlatnessTolerance * kBezierFlatnessTolerance;
    const float control1Distance =
        DistanceToLineSquared(control1, start, end);
    const float control2Distance =
        DistanceToLineSquared(control2, start, end);

    // Cubicは両制御点が弦へ十分近い場合だけ直線近似します。
    // De Casteljau分割を使うことで数値的に安定し、S commandでも同じtessellationを再利用できます。
    if (depth >= kBezierMaxSubdivisionDepth ||
        std::max(control1Distance, control2Distance) <= toleranceSquared)
    {
        AppendPointIfDistinct(outPoints, end);
        return;
    }

    const math::Vec2 p01 = Midpoint(start, control1);
    const math::Vec2 p12 = Midpoint(control1, control2);
    const math::Vec2 p23 = Midpoint(control2, end);
    const math::Vec2 p012 = Midpoint(p01, p12);
    const math::Vec2 p123 = Midpoint(p12, p23);
    const math::Vec2 middle = Midpoint(p012, p123);

    TessellateCubicBezier(start, p01, p012, middle, depth + 1u, outPoints);
    TessellateCubicBezier(middle, p123, p23, end, depth + 1u, outPoints);
}

bool ParsePath(
    const std::string& data,
    std::vector<math::Vec2>& outPoints,
    std::string* outError)
{
    outPoints.clear();
    std::size_t cursor = 0u;
    char command = '\0';
    char previousCommand = '\0';
    math::Vec2 current{};
    math::Vec2 subpathStart{};
    math::Vec2 previousCubicControl{};
    math::Vec2 previousQuadraticControl{};
    bool hasCurrent = false;
    bool hasSubpath = false;
    bool closed = false;

    auto fail = [outError](const std::string& message)
    {
        if (outError != nullptr)
        {
            *outError = message;
        }
        return false;
    };

    while (true)
    {
        SkipSeparators(data, cursor);
        if (cursor >= data.size())
        {
            break;
        }

        const unsigned char token = static_cast<unsigned char>(data[cursor]);
        if (std::isalpha(token) != 0)
        {
            command = data[cursor++];
            if (command == 'Z' || command == 'z')
            {
                if (hasSubpath == false)
                {
                    return fail("SVG path closes before a moveto command.");
                }
                current = subpathStart;
                closed = true;
                previousCommand = command;
                command = '\0';
                continue;
            }

            if (command != 'M' && command != 'm' &&
                command != 'L' && command != 'l' &&
                command != 'H' && command != 'h' &&
                command != 'V' && command != 'v' &&
                command != 'Q' && command != 'q' &&
                command != 'T' && command != 't' &&
                command != 'C' && command != 'c' &&
                command != 'S' && command != 's')
            {
                return fail("SVG path currently supports only M/L/H/V/Q/T/C/S/Z commands.");
            }
        }
        else if (command == '\0')
        {
            return fail("SVG path number appears without an active command.");
        }

        if (closed == true)
        {
            return fail("SVG path currently supports one closed subpath only.");
        }

        if (command == 'M' || command == 'm' ||
            command == 'L' || command == 'l')
        {
            float x = 0.0f;
            float y = 0.0f;
            if (TryReadNumber(data, cursor, x) == false ||
                TryReadNumber(data, cursor, y) == false)
            {
                return fail("SVG path M/L command requires an x/y pair.");
            }

            const char activeCommand = command;
            math::Vec2 next(x, y);
            const bool relative = activeCommand == 'm' || activeCommand == 'l';
            if (relative == true)
            {
                next.x += current.x;
                next.y += current.y;
            }

            if (activeCommand == 'M' || activeCommand == 'm')
            {
                if (hasSubpath == true)
                {
                    return fail("SVG path currently supports one subpath only.");
                }
                subpathStart = next;
                hasSubpath = true;
                command = activeCommand == 'm' ? 'l' : 'L';
            }
            else if (hasCurrent == false)
            {
                return fail("SVG path lineto appears before moveto.");
            }

            current = next;
            hasCurrent = true;
            AppendPointIfDistinct(outPoints, current);
            previousCommand = activeCommand;
            continue;
        }

        if (hasCurrent == false)
        {
            return fail("SVG path command appears before moveto.");
        }

        if (command == 'H' || command == 'h' ||
            command == 'V' || command == 'v')
        {
            float value = 0.0f;
            if (TryReadNumber(data, cursor, value) == false)
            {
                return fail("SVG path H/V command requires a coordinate.");
            }

            if (command == 'H')
            {
                current.x = value;
            }
            else if (command == 'h')
            {
                current.x += value;
            }
            else if (command == 'V')
            {
                current.y = value;
            }
            else
            {
                current.y += value;
            }
            AppendPointIfDistinct(outPoints, current);
            previousCommand = command;
            continue;
        }

        if (command == 'Q' || command == 'q')
        {
            float controlX = 0.0f;
            float controlY = 0.0f;
            float endX = 0.0f;
            float endY = 0.0f;
            if (TryReadNumber(data, cursor, controlX) == false ||
                TryReadNumber(data, cursor, controlY) == false ||
                TryReadNumber(data, cursor, endX) == false ||
                TryReadNumber(data, cursor, endY) == false)
            {
                return fail("SVG path Q command requires control and end x/y pairs.");
            }

            const math::Vec2 segmentStart = current;
            math::Vec2 control(controlX, controlY);
            math::Vec2 end(endX, endY);
            if (command == 'q')
            {
                control.x += segmentStart.x;
                control.y += segmentStart.y;
                end.x += segmentStart.x;
                end.y += segmentStart.y;
            }

            TessellateQuadraticBezier(segmentStart, control, end, 0u, outPoints);
            current = end;
            previousQuadraticControl = control;
            previousCommand = command;
            continue;
        }

        if (command == 'T' || command == 't')
        {
            float endX = 0.0f;
            float endY = 0.0f;
            if (TryReadNumber(data, cursor, endX) == false ||
                TryReadNumber(data, cursor, endY) == false)
            {
                return fail("SVG path T command requires an end x/y pair.");
            }

            const math::Vec2 segmentStart = current;
            const bool followsQuadratic =
                previousCommand == 'Q' || previousCommand == 'q' ||
                previousCommand == 'T' || previousCommand == 't';

            // SVG仕様では直前がQ/T系のときだけ前制御点を現在点の反対側へ鏡映します。
            // それ以外では現在点自身が制御点となり、暗黙制御点を過去の無関係な曲線から引き継ぎません。
            const math::Vec2 control = followsQuadratic == true
                ? ReflectControlPoint(segmentStart, previousQuadraticControl)
                : segmentStart;
            math::Vec2 end(endX, endY);
            if (command == 't')
            {
                end.x += segmentStart.x;
                end.y += segmentStart.y;
            }

            TessellateQuadraticBezier(segmentStart, control, end, 0u, outPoints);
            current = end;
            previousQuadraticControl = control;
            previousCommand = command;
            continue;
        }

        if (command == 'C' || command == 'c')
        {
            float control1X = 0.0f;
            float control1Y = 0.0f;
            float control2X = 0.0f;
            float control2Y = 0.0f;
            float endX = 0.0f;
            float endY = 0.0f;
            if (TryReadNumber(data, cursor, control1X) == false ||
                TryReadNumber(data, cursor, control1Y) == false ||
                TryReadNumber(data, cursor, control2X) == false ||
                TryReadNumber(data, cursor, control2Y) == false ||
                TryReadNumber(data, cursor, endX) == false ||
                TryReadNumber(data, cursor, endY) == false)
            {
                return fail("SVG path C command requires two control pairs and one end pair.");
            }

            const math::Vec2 segmentStart = current;
            math::Vec2 control1(control1X, control1Y);
            math::Vec2 control2(control2X, control2Y);
            math::Vec2 end(endX, endY);
            if (command == 'c')
            {
                control1.x += segmentStart.x;
                control1.y += segmentStart.y;
                control2.x += segmentStart.x;
                control2.y += segmentStart.y;
                end.x += segmentStart.x;
                end.y += segmentStart.y;
            }

            TessellateCubicBezier(segmentStart, control1, control2, end, 0u, outPoints);
            current = end;
            previousCubicControl = control2;
            previousCommand = command;
            continue;
        }

        if (command == 'S' || command == 's')
        {
            float control2X = 0.0f;
            float control2Y = 0.0f;
            float endX = 0.0f;
            float endY = 0.0f;
            if (TryReadNumber(data, cursor, control2X) == false ||
                TryReadNumber(data, cursor, control2Y) == false ||
                TryReadNumber(data, cursor, endX) == false ||
                TryReadNumber(data, cursor, endY) == false)
            {
                return fail("SVG path S command requires control2 and end x/y pairs.");
            }

            const math::Vec2 segmentStart = current;
            const bool followsCubic =
                previousCommand == 'C' || previousCommand == 'c' ||
                previousCommand == 'S' || previousCommand == 's';

            // Sの第1制御点は直前がC/S系の場合だけ第2制御点を鏡映して生成します。
            // 直前が別commandなら現在点を使うため、SVGのsmooth curve規則と一致します。
            const math::Vec2 control1 = followsCubic == true
                ? ReflectControlPoint(segmentStart, previousCubicControl)
                : segmentStart;
            math::Vec2 control2(control2X, control2Y);
            math::Vec2 end(endX, endY);
            if (command == 's')
            {
                control2.x += segmentStart.x;
                control2.y += segmentStart.y;
                end.x += segmentStart.x;
                end.y += segmentStart.y;
            }

            TessellateCubicBezier(segmentStart, control1, control2, end, 0u, outPoints);
            current = end;
            previousCubicControl = control2;
            previousCommand = command;
            continue;
        }
    }

    if (hasSubpath == false || closed == false || outPoints.size() < 3u)
    {
        return fail("SVG path must contain one closed subpath with at least three vertices.");
    }
    return true;
}

math::Vec4 ParseColor(const std::string& text)
{
    if (text == "none")
    {
        return math::Vec4(0.0f, 0.0f, 0.0f, 0.0f);
    }
    if (text == "red")
    {
        return math::Vec4(1.0f, 0.0f, 0.0f, 1.0f);
    }
    if (text == "green")
    {
        return math::Vec4(0.0f, 1.0f, 0.0f, 1.0f);
    }
    if (text == "blue")
    {
        return math::Vec4(0.0f, 0.0f, 1.0f, 1.0f);
    }
    if (text == "black")
    {
        return math::Vec4(0.0f, 0.0f, 0.0f, 1.0f);
    }
    if (text == "white")
    {
        return math::Vec4(1.0f, 1.0f, 1.0f, 1.0f);
    }

    if (text.size() == 7u && text[0] == '#')
    {
        try
        {
            const int red = std::stoi(text.substr(1u, 2u), nullptr, 16);
            const int green = std::stoi(text.substr(3u, 2u), nullptr, 16);
            const int blue = std::stoi(text.substr(5u, 2u), nullptr, 16);
            return math::Vec4(
                static_cast<float>(red) / 255.0f,
                static_cast<float>(green) / 255.0f,
                static_cast<float>(blue) / 255.0f,
                1.0f);
        }
        catch (...)
        {
            return math::Vec4(1.0f, 1.0f, 1.0f, 1.0f);
        }
    }
    return math::Vec4(0.0f, 0.0f, 0.0f, 1.0f);
}

void CollectExistingNames(
    const SvgDocument& document,
    std::unordered_set<std::string>& names)
{
    for (const SvgRectElement& element : document.Rectangles)
    {
        names.insert(element.Name);
    }
    for (const SvgCircleElement& element : document.Circles)
    {
        names.insert(element.Name);
    }
    for (const SvgEllipseElement& element : document.Ellipses)
    {
        names.insert(element.Name);
    }
    for (const SvgLineElement& element : document.Lines)
    {
        names.insert(element.Name);
    }
    for (const SvgPolygonElement& element : document.Polygons)
    {
        names.insert(element.Name);
    }
    for (const SvgPathElement& element : document.Paths)
    {
        names.insert(element.Name);
    }
}

bool AppendOpacityAnimation(
    const std::string& body,
    const std::string& targetPath,
    SvgDocument& document)
{
    const std::regex animateRegex(R"(<animate\b([^>]*)/?>)", std::regex::icase);
    for (std::sregex_iterator it(body.begin(), body.end(), animateRegex), end; it != end; ++it)
    {
        const AttributeMap attributes = ParseAttributes((*it)[1].str());
        const auto nameIt = attributes.find("attributeName");
        const auto fromIt = attributes.find("from");
        const auto toIt = attributes.find("to");
        const auto durationIt = attributes.find("dur");
        if (nameIt == attributes.end() || nameIt->second != "opacity" ||
            fromIt == attributes.end() || toIt == attributes.end() ||
            durationIt == attributes.end())
        {
            continue;
        }

        float from = 0.0f;
        float to = 0.0f;
        float duration = 0.0f;
        try
        {
            from = std::stof(fromIt->second);
            to = std::stof(toIt->second);
            std::string durationText = durationIt->second;
            if (durationText.size() >= 2u &&
                durationText.substr(durationText.size() - 2u) == "ms")
            {
                durationText.resize(durationText.size() - 2u);
                duration = std::stof(durationText) / 1000.0f;
            }
            else
            {
                if (durationText.empty() == false && durationText.back() == 's')
                {
                    durationText.pop_back();
                }
                duration = std::stof(durationText);
            }
        }
        catch (...)
        {
            continue;
        }

        if (duration <= 0.0f)
        {
            continue;
        }

        PropertyAnimationTrack<float> track;
        track.Binding.TargetPath = targetPath;
        track.Binding.Property = "Opacity";
        track.Curve.GetKeys().push_back(AnimationKeyframe<float>{ 0.0f, from });
        track.Curve.GetKeys().push_back(AnimationKeyframe<float>{ duration, to });
        document.Animation.AddPropertyTrack(std::move(track));
        document.Animation.SetDuration(
            std::max(document.Animation.GetDuration(), duration));

        const auto repeatIt = attributes.find("repeatCount");
        if (repeatIt != attributes.end() && repeatIt->second == "indefinite")
        {
            document.LoopAnimation = true;
        }
        return true;
    }
    return true;
}

} // namespace

bool SvgPathImporter::AppendFilePaths(
    const std::string& path,
    SvgDocument& document,
    std::string* outError)
{
    std::string source;
    if (ReadTextFile(path, source) == false)
    {
        if (outError != nullptr)
        {
            *outError = "Failed to open SVG file while importing paths: " + path;
        }
        return false;
    }

    std::unordered_set<std::string> usedNames;
    CollectExistingNames(document, usedNames);
    std::size_t generatedPathIndex = 0u;
    const std::regex pathRegex(
        R"(<path\b([^>]*?)(?:/>|>([\s\S]*?)</path>))",
        std::regex::icase);

    for (std::sregex_iterator it(source.begin(), source.end(), pathRegex), end; it != end; ++it)
    {
        const AttributeMap attributes = ParseAttributes((*it)[1].str());
        const auto dataIt = attributes.find("d");
        if (dataIt == attributes.end())
        {
            if (outError != nullptr)
            {
                *outError = "SVG path requires a d attribute.";
            }
            return false;
        }

        SvgPathElement pathElement;
        if (ParsePath(dataIt->second, pathElement.Points, outError) == false)
        {
            return false;
        }

        const auto idIt = attributes.find("id");
        pathElement.Name = idIt != attributes.end()
            ? idIt->second
            : "path" + std::to_string(generatedPathIndex++);
        if (pathElement.Name.empty() ||
            pathElement.Name.find('/') != std::string::npos ||
            usedNames.insert(pathElement.Name).second == false)
        {
            if (outError != nullptr)
            {
                *outError = "SVG path id is invalid or duplicated: " + pathElement.Name;
            }
            return false;
        }

        const auto fillIt = attributes.find("fill");
        if (fillIt != attributes.end())
        {
            pathElement.FillColor = ParseColor(fillIt->second);
        }

        const std::size_t elementIndex = document.Paths.size();
        document.Paths.push_back(std::move(pathElement));
        document.Shapes.push_back(SvgShapeReference{
            SvgShapeType::Path,
            elementIndex,
            static_cast<std::size_t>(it->position()) });
        AppendOpacityAnimation(
            (*it)[2].str(),
            document.Paths[elementIndex].Name,
            document);
    }

    // 他shapeは既存Importerが型別に解析しているため、path追加後にSourceOffsetで再度統合します。
    std::sort(
        document.Shapes.begin(),
        document.Shapes.end(),
        [](const SvgShapeReference& left, const SvgShapeReference& right)
        {
            return left.SourceOffset < right.SourceOffset;
        });
    return true;
}

} // namespace Raven
