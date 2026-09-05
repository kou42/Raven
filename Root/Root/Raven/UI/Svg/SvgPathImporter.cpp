#include "Raven/UI/Svg/SvgPathImporter.h"

#include "Raven/Animation/AnimationKeyframe.h"
#include "Raven/Animation/AnimationTrack.h"

#include <algorithm>
#include <cctype>
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

AttributeMap ParseAttributes(const std::string& text)
{
    AttributeMap attributes;
    const std::regex attributeRegex(R"(([A-Za-z_:][A-Za-z0-9_.:-]*)\s*=\s*"([^"]*)"))");
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

bool ParseLinearPath(
    const std::string& data,
    std::vector<math::Vec2>& outPoints,
    std::string* outError)
{
    outPoints.clear();
    std::size_t cursor = 0u;
    char command = '\0';
    math::Vec2 current{};
    math::Vec2 subpathStart{};
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
                command = '\0';
                continue;
            }

            if (command != 'M' && command != 'm' &&
                command != 'L' && command != 'l' &&
                command != 'H' && command != 'h' &&
                command != 'V' && command != 'v')
            {
                return fail("SVG path currently supports only M/L/H/V/Z commands.");
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

        if (command == 'M' || command == 'm' || command == 'L' || command == 'l')
        {
            float x = 0.0f;
            float y = 0.0f;
            if (TryReadNumber(data, cursor, x) == false ||
                TryReadNumber(data, cursor, y) == false)
            {
                return fail("SVG path M/L command requires an x/y pair.");
            }

            math::Vec2 next(x, y);
            const bool relative = command == 'm' || command == 'l';
            if (relative == true)
            {
                next.x += current.x;
                next.y += current.y;
            }

            if (command == 'M' || command == 'm')
            {
                if (hasSubpath == true)
                {
                    return fail("SVG path currently supports one subpath only.");
                }
                subpathStart = next;
                hasSubpath = true;
                command = command == 'm' ? 'l' : 'L';
            }
            else if (hasCurrent == false)
            {
                return fail("SVG path lineto appears before moveto.");
            }

            current = next;
            hasCurrent = true;
            outPoints.push_back(current);
            continue;
        }

        if (hasCurrent == false)
        {
            return fail("SVG path H/V command appears before moveto.");
        }

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
        outPoints.push_back(current);
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

void CollectExistingNames(const SvgDocument& document, std::unordered_set<std::string>& names)
{
    for (const SvgRectElement& element : document.Rectangles) { names.insert(element.Name); }
    for (const SvgCircleElement& element : document.Circles) { names.insert(element.Name); }
    for (const SvgEllipseElement& element : document.Ellipses) { names.insert(element.Name); }
    for (const SvgLineElement& element : document.Lines) { names.insert(element.Name); }
    for (const SvgPolygonElement& element : document.Polygons) { names.insert(element.Name); }
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
            fromIt == attributes.end() || toIt == attributes.end() || durationIt == attributes.end())
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
            if (durationText.size() >= 2u && durationText.substr(durationText.size() - 2u) == "ms")
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
        document.Animation.SetDuration(std::max(document.Animation.GetDuration(), duration));

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
    const std::regex pathRegex(R"(<path\b([^>]*?)(?:/>|>([\s\S]*?)</path>))", std::regex::icase);

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
        if (ParseLinearPath(dataIt->second, pathElement.Points, outError) == false)
        {
            return false;
        }

        const auto idIt = attributes.find("id");
        pathElement.Name = idIt != attributes.end()
            ? idIt->second
            : "path" + std::to_string(generatedPathIndex++);
        if (pathElement.Name.empty() || pathElement.Name.find('/') != std::string::npos ||
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
        AppendOpacityAnimation((*it)[2].str(), document.Paths[elementIndex].Name, document);
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
